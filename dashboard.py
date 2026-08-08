#!/usr/bin/env python3
"""Live terminal dashboard for a pi_tree run.

Tails thread_log/run.log (written by run.sh / run_debug.sh) and renders
progress: leaf pieces done/total, an active-worker table, and a timing/ETA
summary.

The expected total piece count and worker count are derived from the
`pi(size, n_process)` call in src/main.c (same formula as get_index_max()
in lib/big/code.c) rather than from the log itself: in a real run the very
first tasks logged are already several tree levels deep (get_next_node
descends silently before the first ready node is handed to a worker), so a
depth-0 log line essentially never appears.

Usage: ./dashboard.py [path/to/run.log] [--size N] [--n-process N] [--main-c PATH]
"""

import argparse
import collections
import os
import re
import signal
import sys
import time

TREE_PIECE_SIZE = 22
PIECES_PER_LEAF = 1 << TREE_PIECE_SIZE

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LOG = os.path.join(REPO_ROOT, "thread_log", "run.log")
DEFAULT_MAIN_C = os.path.join(REPO_ROOT, "src", "main.c")

# node_process/task_start/task_end/split_piece all log "[idx][pid]" - idx is
# the scheduler's reused array slot (see lib/tree/code.c), pid is this task's
# stable identity for its whole lifetime. We key everything off pid.
_TASK_ID = r"\[\s*\d+\]\[\s*(?P<pid>\d+)\]"

RE_NODE_PROCESS = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|\s*"
    r"(?P<i0>\d+)\s+(?P<n2>\d+)\s+(?P<depth>\d+)"
    r"(?:\s*\|\s*(?P<dur>[\d.]+))?"
)
RE_PIECE = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|\s*(?P<i0>\d+)\s+(?P<span>\d+)\s*\|\s*(?P<dur>[\d.]+)"
)
RE_TASK_START = re.compile(_TASK_ID + r"\s*(?P<action>.+?)\s*\|")
RE_TASK_END = re.compile(_TASK_ID + r"\s*(?P<action>.+?)\s*\|(?:.*\|)?\s*(?P<dur>[\d.]+)\s*$")
RE_SCHEDULER = re.compile(r"(?P<action>.+?)\s*\|\s*(?P<val>\d+)")
RE_PHASE = re.compile(r"(?P<action>.+?)\s*\|(?:\s*(?P<dur>[\d.]+))?")

# matches e.g. "pi(4'000'000, 16);" - digit-separator quotes allowed per C23
RE_MAIN_C_CALL = re.compile(r"\bpi\(\s*([\d']+)\s*,\s*([\d']+)\s*\)\s*;")


def get_index_max(size, piece_size=TREE_PIECE_SIZE):
    index_max = (32 * size) + 4
    aux = index_max & ((1 << piece_size) - 1)
    if aux == 0:
        return index_max
    return index_max + (1 << piece_size) - aux


def parse_main_c(path):
    try:
        with open(path, "r") as f:
            text = f.read()
    except OSError:
        return None

    for line in text.splitlines():
        if "uint64_t" in line:  # skip the `static void pi(uint64_t size, ...)` definition
            continue
        m = RE_MAIN_C_CALL.search(line)
        if m:
            size = int(m.group(1).replace("'", ""))
            n_process = int(m.group(2).replace("'", ""))
            return size, n_process
    return None


# A BIG node's "remainder" and a SPAN node's "span" share the same log field
# (n2) with no tag saying which. But remainder is always a multiple of
# PIECES_PER_LEAF (>> 64), while span is a bit-width (<= 64) - the piece-size
# alignment invariant in get_index_max() makes that gap absolute, at any
# depth, so the magnitude alone tells them apart.
SPAN_VS_REMAINDER_CUTOFF = 64


def leaves_covered(n2):
    """How many leaf pieces an "already stored"/"begin" node_process line
    for this n2 value represents - used to credit pieces that were already
    on disk from an earlier run, which never individually log split_piece's
    "piece" line and would otherwise be invisible to the progress count."""
    if n2 > SPAN_VS_REMAINDER_CUTOFF:
        return n2 // PIECES_PER_LEAF
    return 1 << (n2 - TREE_PIECE_SIZE)


def pid_alive(pid):
    """Best-effort OS-level liveness check, since the log alone can't tell
    us a task's process was killed mid-flight - it would just go silent."""
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except OSError:
        # e.g. PermissionError: process exists, we just can't signal it
        return True
    return True


class State:
    def __init__(self, total_pieces=None, n_process=None):
        self.start_time = None
        self.last_line_time = None
        self.total_pieces = total_pieces
        self.n_process = n_process
        self.pieces_done = 0
        self.pieces_from_cache = 0
        # a tree that reduces N leaves down to one root result always does
        # exactly N-1 join operations, regardless of its shape (each join
        # merges two components into one, starting from N components and
        # ending at 1) - so total joins is derived from total_pieces, no
        # separate simulation of the BIG/SPAN tree shape needed.
        self.joins_done = 0
        self.joins_from_cache = 0
        self.piece_events = collections.deque(maxlen=2000)  # (time, dur)
        self.active = {}  # pid -> {"start", "depth", "i0"}
        self.active_max = 0
        self.phase = "splitting"
        self.crashed = collections.deque(maxlen=10)  # (pid, depth, i0, ran_for)
        self.crashed_total = 0
        self.suspected_dead = {}  # pid -> time first seen not-alive
        self.done = False

    def touch(self):
        if self.start_time is None:
            self.start_time = time.time()
        self.last_line_time = time.time()

    @property
    def total_joins(self):
        return self.total_pieces - 1 if self.total_pieces else None


def handle_node_process(state, content):
    m = RE_NODE_PROCESS.match(content)
    if not m:
        return
    pid = int(m.group("pid"))
    action = m.group("action").strip()
    i0 = int(m.group("i0"))
    depth = int(m.group("depth"))

    if action == "begin":
        entry = state.active.setdefault(pid, {"start": time.time()})
        entry["depth"] = depth
        entry["i0"] = i0
    elif action == "already stored":
        state.active.pop(pid, None)
        state.suspected_dead.pop(pid, None)
        # this whole subtree was cached from an earlier (possibly
        # interrupted) run, so it never produces its own "piece"/"joined"
        # lines - credit its leaves *and* the joins that combined them, or
        # the progress count would silently stall on a resumed run.
        covered = leaves_covered(int(m.group("n2")))
        state.pieces_done += covered
        state.pieces_from_cache += covered
        joins_covered = covered - 1  # a subtree of L leaves has L-1 joins
        state.joins_done += joins_covered
        state.joins_from_cache += joins_covered
    elif action == "joined":
        state.joins_done += 1


def handle_piece(state, content):
    m = RE_PIECE.match(content)
    if not m or m.group("action").strip() != "piece":
        return
    dur = float(m.group("dur"))
    state.pieces_done += 1
    state.piece_events.append((time.time(), dur))


def handle_task_start(state, content):
    m = RE_TASK_START.match(content)
    if not m:
        return
    pid = int(m.group("pid"))
    state.active.setdefault(pid, {"start": time.time(), "depth": None, "i0": None})


def handle_task_end(state, content):
    m = RE_TASK_END.match(content)
    if not m:
        return
    pid = int(m.group("pid"))
    state.active.pop(pid, None)
    state.suspected_dead.pop(pid, None)


def handle_scheduler(state, content):
    m = RE_SCHEDULER.match(content)
    if not m:
        return
    action = m.group("action").strip()
    if action == "active processes":
        state.active_max = max(state.active_max, int(m.group("val")))
    elif action in ("pi already stored", "binary split solved"):
        state.phase = action


def handle_phase(state, content):
    m = RE_PHASE.match(content)
    if not m:
        return
    action = m.group("action").strip()
    if action in ("dividing", "divided", "pi already stored", "binary split solved"):
        state.phase = action
    if action == "divided":
        state.done = True
    if action == "pi already stored":
        # pi_tree() short-circuits here without ever running the scheduler -
        # the whole result was already on disk from an earlier run.
        state.done = True
        if state.total_pieces:
            state.pieces_from_cache += state.total_pieces - state.pieces_done
            state.pieces_done = state.total_pieces
            state.joins_from_cache += state.total_joins - state.joins_done
            state.joins_done = state.total_joins
            state.pieces_done = state.total_pieces


DISPATCH = {
    "node_process": handle_node_process,
    "split_piece": handle_piece,
    "task_start": handle_task_start,
    "task_end": handle_task_end,
    "scheduler": handle_scheduler,
    "pi_tree": handle_phase,
    "pi_finish": handle_phase,
    "pi_big": handle_phase,
}


def feed_line(state, line):
    line = line.rstrip("\n")
    # the func/content separator is "\t| ", but some pipe/pty layers between
    # the C program and this log file expand that tab to spaces, so split on
    # the first literal "|" instead (func names never contain one) and let
    # the per-func regexes absorb whatever whitespace remains.
    func, sep, content = line.partition("|")
    if not sep:
        return
    func = func.strip()
    content = content.strip()
    handler = DISPATCH.get(func)
    if handler is None:
        return
    state.touch()
    handler(state, content)


# A task's process legitimately disappears (gets reaped by the parent's
# waitpid()) an instant *before* the parent's own "task_end" log line for it
# is written and flows through the tee pipe to this file - so "not found by
# pid_alive()" is also exactly what a completely normal finish looks like
# for a brief moment, not just a crash. Debounce past that window before
# believing it, or every successful completion would flash as a false
# "crashed" that then never gets reclaimed once task_end no-ops on it.
DEAD_GRACE_SECONDS = 3.0


def sweep_dead(state):
    """Move tasks whose process has been gone for longer than the log could
    plausibly still catch up on into `crashed`, since the log itself never
    tells us a task was killed - it would otherwise sit in `active` forever
    looking healthy."""
    now = time.time()
    for pid in list(state.active):
        if pid_alive(pid):
            state.suspected_dead.pop(pid, None)
            continue

        first_seen = state.suspected_dead.setdefault(pid, now)
        if now - first_seen < DEAD_GRACE_SECONDS:
            continue

        w = state.active.pop(pid)
        state.suspected_dead.pop(pid, None)
        state.crashed.append((pid, w["depth"], w["i0"], now - w["start"]))
        state.crashed_total += 1


def fmt_duration(seconds):
    if seconds is None or seconds != seconds or seconds == float("inf"):
        return "?"
    seconds = int(seconds)
    h, rem = divmod(seconds, 3600)
    m, s = divmod(rem, 60)
    if h:
        return f"{h:d}:{m:02d}:{s:02d}"
    return f"{m:02d}:{s:02d}"


def render(state):
    lines = []
    lines.append("=== pi_tree dashboard ===  " + time.strftime("%H:%M:%S"))
    lines.append("")

    now = time.time()
    elapsed = now - state.start_time if state.start_time else 0
    lines.append(f"phase:   {state.phase}")
    lines.append(f"elapsed: {fmt_duration(elapsed)} (since dashboard attached)")

    if state.last_line_time is not None and not state.done:
        since_last_line = now - state.last_line_time
        recent_durs = [d for _, d in state.piece_events]
        # a lull up to one full task duration is normal (e.g. only one slow
        # task left running); only warn once we're well past that.
        stall_threshold = max(60.0, 2 * max(recent_durs)) if recent_durs else 60.0
        if since_last_line > stall_threshold:
            lines.append(
                f"WARNING: no log activity for {fmt_duration(since_last_line)} "
                "- the run may have stopped or crashed"
            )

    lines.append("")

    cached_units = state.pieces_from_cache + state.joins_from_cache
    cache_note = f"  ({cached_units} from cache)" if cached_units else ""
    if state.total_pieces:
        total_joins = state.total_joins
        total_units = state.total_pieces + total_joins
        done_units = min(state.pieces_done + state.joins_done, total_units)
        pct = 100.0 * done_units / total_units
        bar_width = 40
        filled = int(bar_width * done_units / total_units)
        bar = "#" * filled + "-" * (bar_width - filled)
        lines.append(f"pieces:  {state.pieces_done} / {state.total_pieces}    joins: {state.joins_done} / {total_joins}{cache_note}")
        lines.append(f"overall: {done_units} / {total_units}  ({pct:5.1f}%)")
        lines.append(f"         [{bar}]")
    else:
        lines.append(f"pieces:  {state.pieces_done} / ?    joins: {state.joins_done} / ? (pass --size for totals){cache_note}")
    lines.append("")

    n_workers = state.n_process or state.active_max
    crashed_note = f"  ({state.crashed_total} crashed total)" if state.crashed_total else ""
    lines.append(f"active workers ({len(state.active)}/{n_workers or '?'}){crashed_note}:")
    lines.append(f"  {'pid':>7}  {'elapsed':>8}  {'depth':>5}  {'i0':>14}")
    for pid, w in sorted(state.active.items()):  # ordered by task id (pid)
        depth = w["depth"] if w["depth"] is not None else "-"
        i0 = f"{w['i0']:,}" if w["i0"] is not None else "-"
        lines.append(f"  {pid:>7}  {fmt_duration(now - w['start']):>8}  {depth:>5}  {i0:>14}")
    if not state.active:
        lines.append("  (none)")

    if state.crashed:
        lines.append("")
        lines.append("crashed workers (pid, depth, i0, ran for):")
        for pid, depth, i0, ran_for in state.crashed:
            depth = depth if depth is not None else "-"
            i0 = f"{i0:,}" if i0 is not None else "-"
            lines.append(f"  {pid:<7} depth={depth!s:<3} i0={i0:<14} {fmt_duration(ran_for)}")

    return "\n".join(lines)


def tail(path):
    while not os.path.exists(path):
        yield None
        time.sleep(0.3)

    with open(path, "r", errors="replace") as f:
        while True:
            line = f.readline()
            if line:
                yield line
            else:
                yield None
                time.sleep(0.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_path", nargs="?", default=DEFAULT_LOG)
    parser.add_argument("--size", type=int, default=None, help="pi() size argument, to compute total pieces")
    parser.add_argument("--n-process", type=int, default=None, help="pi() n_process argument")
    parser.add_argument("--main-c", default=DEFAULT_MAIN_C, help="path to src/main.c, auto-parsed if --size not given")
    args = parser.parse_args()

    size, n_process = args.size, args.n_process
    if size is None or n_process is None:
        parsed = parse_main_c(args.main_c)
        if parsed:
            size = size if size is not None else parsed[0]
            n_process = n_process if n_process is not None else parsed[1]

    total_pieces = get_index_max(size) // PIECES_PER_LEAF if size is not None else None
    state = State(total_pieces=total_pieces, n_process=n_process)

    # draw in the terminal's alternate screen buffer (like htop/less) so
    # repeated redraws overwrite in place instead of scrolling into history -
    # a plain "\x1b[2J\x1b[H" only clears the visible screen, not scrollback,
    # so fast redraws pile up into a wall of stacked frames there.
    sys.stdout.write("\x1b[?1049h\x1b[?25l")
    sys.stdout.flush()
    try:
        last_render = 0.0
        for line in tail(args.log_path):
            if line is not None:
                feed_line(state, line)

            now = time.time()
            if now - last_render >= 1.0:
                sweep_dead(state)
                sys.stdout.write("\x1b[H\x1b[2J")
                sys.stdout.write(render(state))
                sys.stdout.write("\n")
                sys.stdout.flush()
                last_render = now

            if state.done:
                time.sleep(1.0)
                break
    finally:
        sys.stdout.write("\x1b[?25h\x1b[?1049l")
        sys.stdout.flush()


def _raise_keyboard_interrupt(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    # SIGTERM bypasses Python's finally blocks by default; route it through
    # the same cleanup path as Ctrl+C so a `kill` or supervisor stop still
    # restores the terminal out of the alternate screen buffer.
    signal.signal(signal.SIGTERM, _raise_keyboard_interrupt)
    try:
        main()
    except KeyboardInterrupt:
        pass
