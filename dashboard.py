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
import sys
import time

TREE_PIECE_SIZE = 22
PIECES_PER_LEAF = 1 << TREE_PIECE_SIZE

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LOG = os.path.join(REPO_ROOT, "thread_log", "run.log")
DEFAULT_MAIN_C = os.path.join(REPO_ROOT, "src", "main.c")

RE_NODE_PROCESS = re.compile(
    r"\[\s*(?P<idx>\d+)\]\s*(?P<action>.{1,16}?)\s*\|\s*"
    r"(?P<i0>\d+)\s+(?P<n2>\d+)\s+(?P<depth>\d+)"
    r"(?:\s*\|\s*(?P<dur>[\d.]+))?"
)
RE_PIECE = re.compile(
    r"(?P<action>.{1,16}?)\s*\|\s*(?P<i0>\d+)\s+(?P<span>\d+)\s*\|\s*(?P<dur>[\d.]+)"
)
RE_TASK_START = re.compile(r"\[\s*(?P<idx>\d+)\]\s*(?P<action>.{1,16}?)\s*\|")
RE_TASK_END = re.compile(r"\[\s*(?P<idx>\d+)\]\s*(?P<action>.{1,16}?)\s*\|(?:.*\|)?\s*(?P<dur>[\d.]+)\s*$")
RE_SCHEDULER = re.compile(r"(?P<action>.{1,16}?)\s*\|\s*(?P<val>\d+)")
RE_PHASE = re.compile(r"(?P<action>.{1,16}?)\s*\|(?:\s*(?P<dur>[\d.]+))?")

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


class State:
    def __init__(self, total_pieces=None, n_process=None):
        self.start_time = None
        self.total_pieces = total_pieces
        self.n_process = n_process
        self.pieces_done = 0
        self.piece_events = collections.deque(maxlen=2000)  # (time, dur)
        self.workers = {}  # idx -> dict(desc, start)
        self.active_max = 0
        self.phase = "splitting"
        self.recent_pieces = collections.deque(maxlen=10)
        self.recent_task_ends = collections.deque(maxlen=10)
        self.done = False

    def touch(self):
        if self.start_time is None:
            self.start_time = time.time()


def handle_node_process(state, content):
    m = RE_NODE_PROCESS.match(content)
    if not m:
        return
    idx = int(m.group("idx"))
    action = m.group("action").strip()
    i0 = int(m.group("i0"))
    depth = int(m.group("depth"))

    if action == "begin":
        state.workers[idx] = {
            "desc": f"i0={i0} depth={depth}",
            "start": state.workers.get(idx, {}).get("start", time.time()),
        }
    elif action == "already stored":
        state.workers.pop(idx, None)


def handle_piece(state, content):
    m = RE_PIECE.match(content)
    if not m or m.group("action").strip() != "piece":
        return
    dur = float(m.group("dur"))
    state.pieces_done += 1
    now = time.time()
    state.piece_events.append((now, dur))
    state.recent_pieces.append((int(m.group("i0")), int(m.group("span")), dur))


def handle_task_start(state, content):
    m = RE_TASK_START.match(content)
    if not m:
        return
    idx = int(m.group("idx"))
    state.workers.setdefault(idx, {"desc": "starting...", "start": time.time()})
    state.active_max = max(state.active_max, idx + 1)


def handle_task_end(state, content):
    m = RE_TASK_END.match(content)
    if not m:
        return
    idx = int(m.group("idx"))
    dur = float(m.group("dur"))
    state.workers.pop(idx, None)
    state.recent_task_ends.append((idx, dur))


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


def pieces_per_sec(state, window=15.0):
    now = time.time()
    cutoff = now - window
    recent = [d for (t, d) in state.piece_events if t >= cutoff]
    if len(recent) < 2:
        return 0.0
    span = now - next(t for (t, d) in state.piece_events if t >= cutoff)
    return len(recent) / max(span, 1.0)


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

    elapsed = time.time() - state.start_time if state.start_time else 0
    lines.append(f"phase:   {state.phase}")
    lines.append(f"elapsed: {fmt_duration(elapsed)} (since dashboard attached)")
    lines.append("")

    if state.total_pieces:
        pct = 100.0 * state.pieces_done / state.total_pieces
        bar_width = 40
        filled = int(bar_width * min(state.pieces_done, state.total_pieces) / state.total_pieces)
        bar = "#" * filled + "-" * (bar_width - filled)
        lines.append(f"pieces:  {state.pieces_done} / {state.total_pieces}  ({pct:5.1f}%)")
        lines.append(f"         [{bar}]")
    else:
        lines.append(f"pieces:  {state.pieces_done} / ? (pass --size to compute a total)")

    rate = pieces_per_sec(state)
    remaining = (state.total_pieces - state.pieces_done) if state.total_pieces else None
    eta = (remaining / rate) if (rate > 0 and remaining is not None) else None
    lines.append(f"rate:    {rate:5.2f} pieces/s        eta: {fmt_duration(eta)}")
    lines.append("")

    n_workers = state.n_process or max(state.active_max, len(state.workers))
    lines.append(f"active workers ({len(state.workers)}/{n_workers or '?'}):")
    lines.append(f"  {'#':>3} {'elapsed':>8}  detail")
    now = time.time()
    for idx in sorted(state.workers):
        w = state.workers[idx]
        lines.append(f"  {idx:>3} {fmt_duration(now - w['start']):>8}  {w['desc']}")
    if not state.workers:
        lines.append("  (none)")
    lines.append("")

    lines.append("recent pieces (i0, span, seconds):")
    if state.recent_pieces:
        for i0, span, dur in list(state.recent_pieces)[-10:]:
            lines.append(f"  i0={i0:<12} span={span:<3} {dur:7.1f}s")
    else:
        lines.append("  (none yet)")
    lines.append("")

    lines.append("recent task ends (worker, seconds):")
    if state.recent_task_ends:
        for idx, dur in list(state.recent_task_ends)[-10:]:
            lines.append(f"  #{idx:<3} {dur:7.1f}s")
    else:
        lines.append("  (none yet)")

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

    last_render = 0.0
    for line in tail(args.log_path):
        if line is not None:
            feed_line(state, line)

        now = time.time()
        if now - last_render >= 0.25:
            sys.stdout.write("\x1b[2J\x1b[H")
            sys.stdout.write(render(state))
            sys.stdout.write("\n")
            sys.stdout.flush()
            last_render = now

        if state.done:
            break


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
