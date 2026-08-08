#!/usr/bin/env python3
"""Live terminal dashboard for a pi_tree run.

Tails thread_log/run.log (written by run.sh / run_debug.sh) and renders
progress: leaf pieces done/total, an active-worker count, and a live tree
view with per-node elapsed time.

The expected total piece count is derived from the `size` argument of the
`pi(size, n_process)` call in src/main.c (same formula as get_index_max()
in lib/big/code.c) rather than from the log itself: in a real run the very
first tasks logged are already several tree levels deep (get_next_node
descends silently before the first ready node is handed to a worker), so a
depth-0 log line essentially never appears.

Worker count is never read from source (n_process isn't always a literal -
it can be computed per-platform, for instance). It comes from --n-process
if given, otherwise it's inferred live from the log's "active processes"
line, which converges to the true value within the first scheduling cycle.

Usage: ./dashboard.py [path/to/run.log] [--size N] [--n-process N] [--main-c PATH]
"""

import argparse
import collections
import os
import re
import shutil
import signal
import subprocess
import sys
import termios
import time
import tty

TREE_PIECE_SIZE = 22
PIECES_PER_LEAF = 1 << TREE_PIECE_SIZE

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LOG = os.path.join(REPO_ROOT, "thread_log", "run.log")
DEFAULT_MAIN_C = os.path.join(REPO_ROOT, "src", "main.c")

# node_process/task_start/task_end/split_piece all log "[idx][pid]" - idx is
# the scheduler's reused array slot (see lib/tree/code.c), pid is this task's
# stable identity for its whole lifetime. We key everything off pid.
_TASK_ID = r"\[\s*(?P<idx>\d+)\]\[\s*(?P<pid>\d+)\]"

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

# matches the size literal in e.g. "pi(4'000'000, 16);" or
# "pi(4'000'000, n_threads);" (digit-separator quotes allowed per C23) -
# n_process is deliberately not captured here, see parse_main_c().
RE_MAIN_C_CALL = re.compile(r"\bpi\(\s*([\d']+)\s*,")


def get_index_max(size, piece_size=TREE_PIECE_SIZE):
    index_max = (32 * size) + 4
    aux = index_max & ((1 << piece_size) - 1)
    if aux == 0:
        return index_max
    return index_max + (1 << piece_size) - aux


def parse_main_c(path):
    """Extract `size` from the `pi(size, n_process)` call in main.c. Only
    size is read from source: n_process isn't always a literal (e.g. it can
    be computed per-platform), so it's left to --n-process or, absent that,
    to State.active_max tracking the real concurrency seen in the log."""
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
            return int(m.group(1).replace("'", ""))
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


# The log only ever tells us which nodes were *touched*, never the tree's
# shape - but the shape is fully deterministic from index_max alone (same
# recursion as node_big_create/node_span_create/node_expand in
# lib/tree/code.c), so we replicate it here once and overlay live status
# from the log onto it by key, rather than trying to infer structure from
# the log's begin/joining/joined lines.
TREE_NODE_CAP = 20000  # total nodes; skip the view rather than choke on it


class TreeNode:
    __slots__ = (
        "i0", "depth", "kind", "n2", "leaves_total", "leaves_done",
        "own_done", "in_progress", "task_idx", "start_time", "active_count", "parent", "children",
    )

    def __init__(self, i0, depth, kind, n2, parent):
        self.i0 = i0
        self.depth = depth
        self.kind = kind  # "BIG" or "SPAN"
        self.n2 = n2  # remainder (BIG) or span (SPAN)
        self.leaves_total = (n2 // PIECES_PER_LEAF) if kind == "BIG" else (1 << (n2 - TREE_PIECE_SIZE))
        self.leaves_done = 0
        # leaves_done reaching leaves_total only means every *leaf*
        # underneath is computed - the "joining" steps that actually
        # combine them into THIS node's own result run afterward, and can
        # take a while. own_done is set only by this exact node's own
        # completion event (piece/already-stored/joined) and is the real
        # "collapse this subtree" signal for the tree view.
        self.own_done = False
        self.in_progress = False
        # the scheduler's array slot ([idx] in the log) currently working
        # this node - only meaningful while in_progress; set at "begin",
        # cleared once this node's own completion event fires.
        self.task_idx = None
        # wall-clock time.time() this node's own task started, for the
        # elapsed time shown next to it in the tree view; same lifetime as
        # task_idx.
        self.start_time = None
        # count of currently-running tasks anywhere in this node's subtree
        # (itself included). get_next_node descends silently to find a
        # ready node before logging anything, so an ancestor sits at
        # "pending" - no begin/joined line of its own - for the node's
        # entire lifetime; without this, the tree view has no way to tell
        # "pending, nothing happening below" apart from "pending, but N
        # tasks are actively working several levels down" and collapses
        # both the same way.
        self.active_count = 0
        self.parent = parent
        self.children = []


def _build_span(i0, span, depth, parent, by_key):
    node = TreeNode(i0, depth, "SPAN", span, parent)
    by_key[(i0, depth)] = node
    if span > TREE_PIECE_SIZE:
        half = span - 1
        node.children = [
            _build_span(i0, half, depth + 1, node, by_key),
            _build_span(i0 + (1 << half), half, depth + 1, node, by_key),
        ]
    return node


def _build_big(i0, remainder, depth, parent, by_key):
    if bin(remainder).count("1") == 1:
        return _build_span(i0, remainder.bit_length() - 1, depth, parent, by_key)

    node = TreeNode(i0, depth, "BIG", remainder, parent)
    by_key[(i0, depth)] = node
    span = remainder.bit_length() - 1
    node.children = [
        _build_span(i0, span, depth + 1, node, by_key),
        _build_big(i0 + (1 << span), remainder - (1 << span), depth + 1, node, by_key),
    ]
    return node


def build_tree(index_max):
    """Returns (root, by_key) where by_key maps (i0, depth) -> TreeNode, or
    (None, None) if the tree is too large to materialize/render sanely."""
    total_pieces = index_max // PIECES_PER_LEAF
    if 2 * total_pieces - 1 > TREE_NODE_CAP:
        return None, None
    by_key = {}
    root = _build_big(1, index_max, 0, None, by_key)
    return root, by_key


def mark_leaves_done(node, leaves):
    """Credit a completed leaf count up through every ancestor, purely for
    the "X/Y leaves done" partial-progress number - this alone does NOT
    mean an ancestor's own combine step has run, so it must never by
    itself cause a node to collapse as done in the tree view."""
    n = node
    while n is not None:
        n.leaves_done = min(n.leaves_done + leaves, n.leaves_total)
        n = n.parent


def mark_active(node, delta):
    """Propagate a task starting (+1) or finishing (-1) up through every
    ancestor's active_count, so an ancestor that hasn't logged anything of
    its own yet still knows work is happening in its subtree."""
    n = node
    while n is not None:
        n.active_count += delta
        n = n.parent


def mark_node_done(node):
    """This exact node's own result just completed (piece / already stored
    / joined) - only this node collapses; ancestors still need their own
    completion event before they do too."""
    node.own_done = True
    node.in_progress = False
    node.task_idx = None
    node.start_time = None
    mark_active(node, -1)


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


def pi_process_running():
    """True if a pi_tree binary (src/main.o or src/debug.o) is currently
    running. thread_log/run.log from a finished run looks byte-for-byte
    identical whether it's from a live run that just wrapped up or a stale
    leftover from an earlier one with nothing running right now - only the
    OS can tell those apart."""
    try:
        result = subprocess.run(
            ["pgrep", "-f", r"src/(main|debug)\.o"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return result.returncode == 0
    except OSError:
        return False  # no pgrep available - fail closed rather than false-claim "running"


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
        self.tree_root = None
        self.tree_by_key = None  # (i0, depth) -> TreeNode
        self.tree_skipped_reason = None
        self.process_running = False
        self.ever_saw_process = False
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
        # split_piece's "piece" line is logged by tprintf from inside
        # node_process (case NODE_SPAN, right after calling split_piece()),
        # so its __func__ - and thus its log func field - is "node_process",
        # not "split_piece"; RE_NODE_PROCESS doesn't match it because it has
        # no depth field. Route it to handle_piece instead of dropping it.
        handle_piece(state, content)
        return
    pid = int(m.group("pid"))
    idx = int(m.group("idx"))
    action = m.group("action").strip()
    i0 = int(m.group("i0"))
    depth = int(m.group("depth"))

    tree_node = state.tree_by_key.get((i0, depth)) if state.tree_by_key else None

    if action == "begin":
        entry = state.active.setdefault(pid, {"start": time.time()})
        entry["depth"] = depth
        entry["i0"] = i0
        if tree_node is not None:
            tree_node.in_progress = True
            tree_node.task_idx = idx
            tree_node.start_time = entry["start"]
            mark_active(tree_node, 1)
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
        if tree_node is not None:
            mark_leaves_done(tree_node, covered)
            mark_node_done(tree_node)  # cached result stands in for this node's own join
    elif action == "joined":
        state.joins_done += 1
        if tree_node is not None:
            mark_node_done(tree_node)


def handle_piece(state, content):
    m = RE_PIECE.match(content)
    if not m or m.group("action").strip() != "piece":
        return
    dur = float(m.group("dur"))
    state.pieces_done += 1
    state.piece_events.append((time.time(), dur))

    if state.tree_by_key:
        # split_piece's own log line has no depth field (only i0/span) -
        # the "begin" line the same pid logged moments earlier does, so
        # pull it from there.
        i0 = int(m.group("i0"))
        entry = state.active.get(int(m.group("pid")))
        depth = entry["depth"] if entry else None
        tree_node = state.tree_by_key.get((i0, depth)) if depth is not None else None
        if tree_node is not None:
            mark_leaves_done(tree_node, 1)
            mark_node_done(tree_node)  # a leaf has no separate join step - computing it is completing it


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
        if state.tree_root is not None:
            mark_leaves_done(state.tree_root, state.tree_root.leaves_total)
            mark_node_done(state.tree_root)


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


def render_tree(node, lines, now, prefix="", is_last=True, is_root=True):
    if node.own_done:
        mark, state, status = "✓", "done", None
    elif node.in_progress:
        mark, state, status = "▸", "in_progress", str(node.task_idx)
    elif node.leaves_done > 0:
        mark, state, status = "·", "in_progress", None
    elif node.active_count > 0:
        # this node itself hasn't been scheduled yet (no begin/joined line
        # of its own) - a descendant mark (▸ N) already shows what's
        # actually running, so this one just needs a mark distinct from
        # "pending" to say "don't collapse me, there's activity below".
        mark, state, status = "○", "in_progress", None
    else:
        mark, state, status = "·", "pending", None

    connector = "" if is_root else ("└─ " if is_last else "├─ ")
    label = f"depth={node.depth} i0={node.i0:,}"
    if status is not None:
        label += f" [{status}]"
        if node.start_time is not None:
            label += f" {fmt_duration(now - node.start_time)}"
    lines.append(f"{prefix}{connector}{mark} {label}")

    if state in ("done", "pending"):
        return  # collapse: a done subtree needs no detail, a pending one has none yet

    child_prefix = prefix if is_root else prefix + ("   " if is_last else "│  ")
    for i, child in enumerate(node.children):
        render_tree(child, lines, now, child_prefix, i == len(node.children) - 1, is_root=False)


def render_status_screen(state):
    """A stale thread_log/run.log from a previous run reads identically to
    a live one that just finished, so `state.done` alone can't be trusted
    here - trust the OS (state.ever_saw_process/process_running) instead:
    with no pi_tree process ever actually seen running this session, show
    what's really going on rather than a full dashboard built from
    leftover/misleading numbers."""
    lines = []
    lines.append("=== pi_tree dashboard ===  " + time.strftime("%H:%M:%S"))
    lines.append("")
    if not state.ever_saw_process:
        lines.append("  waiting for process...")
        lines.append("  (no pi_tree process detected - start it with ./run.sh or ./run_debug.sh)")
    elif state.done:
        lines.append("  process finished.")
    else:
        lines.append("  WARNING: process is no longer running, but never finished")
        lines.append("  (it may have crashed - check thread_log/run.log for the last lines)")
    return "\n".join(lines)


def render(state):
    if not state.ever_saw_process or (not state.process_running and not state.done):
        return render_status_screen(state)

    lines = []
    lines.append("=== pi_tree dashboard ===  " + time.strftime("%H:%M:%S"))
    lines.append("")

    now = time.time()
    elapsed = now - state.start_time if state.start_time else 0
    lines.append(f"phase:   {state.phase}")
    lines.append(f"elapsed: {fmt_duration(elapsed)} (since dashboard attached)")

    if state.done:
        lines.append("")
        lines.append("*** PROCESS DONE - press any key to exit ***")

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
    lines.append(f"active workers: {len(state.active)}/{n_workers or '?'}{crashed_note}")

    if state.crashed:
        lines.append("")
        lines.append("crashed workers (pid, depth, i0, ran for):")
        for pid, depth, i0, ran_for in state.crashed:
            depth = depth if depth is not None else "-"
            i0 = f"{i0:,}" if i0 is not None else "-"
            lines.append(f"  {pid:<7} depth={depth!s:<3} i0={i0:<14} {fmt_duration(ran_for)}")

    lines.append("")
    if state.tree_root is not None:
        lines.append("tree (finished/pending subtrees collapsed):")
        render_tree(state.tree_root, lines, now)
    elif state.tree_skipped_reason:
        lines.append(f"tree: {state.tree_skipped_reason}")

    return "\n".join(lines)


# yielded by tail() when the file at `path` got replaced by a new one (a
# different inode) - run.sh does `rm -rf thread_log/*` at the start of
# every invocation, so a dashboard already attached from before a run
# started is holding a handle to what's now a deleted file: reads on it
# just return EOF forever, never seeing the new run's actual content, while
# whatever state.done/etc. it parsed from the old file's leftovers (if any)
# stays stuck. The caller must throw its State away and start fresh.
RESTARTED = object()


def tail(path):
    inode = None
    f = None
    try:
        while True:
            try:
                st = os.stat(path)
            except OSError:
                if f is not None:
                    f.close()
                    f = None
                    inode = None
                    yield RESTARTED
                yield None
                time.sleep(0.3)
                continue

            if f is None or st.st_ino != inode:
                if f is not None:
                    f.close()
                f = open(path, "r", errors="replace")
                inode = st.st_ino
                yield RESTARTED

            line = f.readline()
            if line:
                yield line
            else:
                yield None
                time.sleep(0.2)
    finally:
        if f is not None:
            f.close()


def draw(state):
    cols, rows = shutil.get_terminal_size(fallback=(80, 24))
    # pad every row out to the full pane size ourselves rather than relying
    # on \x1b[2J to fill erased cells with the active background color - not
    # all terminals honor that ("back color erase"), so unpainted cells
    # would otherwise show through as the terminal's default background.
    content = [line[:cols].ljust(cols) for line in render(state).split("\n")][:rows]
    content += [" " * cols] * (rows - len(content))
    sys.stdout.write("\x1b[H\x1b[2J")
    sys.stdout.write("\n".join(content))
    sys.stdout.flush()


def wait_for_dismissal():
    """Block until the user presses a key, so the finished dashboard's
    final frame stays on screen instead of the alternate-screen buffer
    getting torn down (and the terminal snapping back to the shell) the
    instant the run completes."""
    try:
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
    except (termios.error, OSError, ValueError):
        return  # not an interactive terminal (e.g. stdin piped/redirected)
    try:
        tty.setcbreak(fd)
        os.read(fd, 1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_path", nargs="?", default=DEFAULT_LOG)
    parser.add_argument("--size", type=int, default=None, help="pi() size argument, to compute total pieces")
    parser.add_argument("--n-process", type=int, default=None, help="pi() n_process argument")
    parser.add_argument("--main-c", default=DEFAULT_MAIN_C, help="path to src/main.c, auto-parsed if --size not given")
    args = parser.parse_args()

    size, n_process = args.size, args.n_process
    if size is None:
        size = parse_main_c(args.main_c)

    total_pieces = get_index_max(size) // PIECES_PER_LEAF if size is not None else None

    def make_state():
        state = State(total_pieces=total_pieces, n_process=n_process)
        if size is not None:
            state.tree_root, state.tree_by_key = build_tree(get_index_max(size))
            if state.tree_root is None:
                state.tree_skipped_reason = f"tree too large to display ({2 * total_pieces - 1} nodes > {TREE_NODE_CAP})"
        return state

    state = make_state()

    # draw in the terminal's alternate screen buffer (like htop/less) so
    # repeated redraws overwrite in place instead of scrolling into history -
    # a plain "\x1b[2J\x1b[H" only clears the visible screen, not scrollback,
    # so fast redraws pile up into a wall of stacked frames there.
    sys.stdout.write("\x1b[?1049h\x1b[?25l\x1b[48;2;10;20;60m")
    sys.stdout.flush()
    try:
        last_render = 0.0
        for line in tail(args.log_path):
            if line is RESTARTED:
                # thread_log/run.log got (re)created since we last opened it
                # - run.sh wipes it at the start of every invocation, so a
                # dashboard already attached from before a run started would
                # otherwise keep reading a deleted file forever while holding
                # onto whatever state (including a stale state.done=True) it
                # parsed from the previous run's leftovers.
                state = make_state()
                continue
            if line is not None:
                feed_line(state, line)

            now = time.time()
            if now - last_render >= 1.0:
                state.process_running = pi_process_running()
                if state.process_running:
                    state.ever_saw_process = True
                sweep_dead(state)
                draw(state)
                last_render = now

            if state.done and state.ever_saw_process:
                # re-draw unconditionally: the periodic draw() above is
                # throttled to once/sec, so the frame the user was looking
                # at when the run finished may predate state.done and still
                # be missing the "PROCESS DONE" banner render() adds for it.
                draw(state)
                wait_for_dismissal()
                break
    finally:
        sys.stdout.write("\x1b[0m\x1b[?25h\x1b[?1049l")
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
