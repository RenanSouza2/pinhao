#!/usr/bin/env python3
"""Live terminal dashboard for a pi_tree run.

Tails thread_log/run.log (written by run.sh / run_debug.sh) and renders
progress: leaf pieces done/total, an active-worker count, and a live tree
view with per-node elapsed time.

The expected total piece count comes from index_max, and the tree shape
comes from TREE_PIECE_SIZE - both logged by pi_tree(), first thing, as
"piece size" and "run size" lines (see lib/tree/code.c), not read from
source. Source is the wrong place to look: TREE_PIECE_SIZE is a build-time
#define a Python process has no access to at all, and size (used to derive
index_max) is whatever will be compiled into the *next* build, not
necessarily what a currently-running binary was actually built with - a
dashboard left attached across an edit+rerun would otherwise silently show
stats for the old build forever. The log lines are this run's own ground
truth, always.

Worker count is never read from source either (n_process isn't always a
literal - it can be computed per-platform, for instance). It comes from
--n-process if given, otherwise it's inferred live from the log's "active
processes" line, which converges to the true value within the first
scheduling cycle.

Usage: ./dashboard.py [path/to/run.log] [--size N] [--n-process N]
"""

import argparse
import collections
import os
import re
import shutil
import signal
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LOG = os.path.join(REPO_ROOT, "thread_log", "run.log")

# Set once the log's own "piece size" line arrives (see apply_piece_size /
# RE_PIECE_SIZE below) - None until then. A build-time #define, so this is
# the only place a Python process can ever learn it; there is no sane
# default to fall back on.
TREE_PIECE_SIZE = None
PIECES_PER_LEAF = None


def apply_piece_size(piece_size):
    global TREE_PIECE_SIZE, PIECES_PER_LEAF
    TREE_PIECE_SIZE = piece_size
    PIECES_PER_LEAF = 1 << piece_size

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

# pi_tree()'s one-time "piece size" and "run size" lines - see the module
# docstring for why these, and not source, are where TREE_PIECE_SIZE and
# index_max come from. "piece size" is always logged first (see the ordering
# comment in lib/tree/code.c), so by the time "run size" is seen,
# TREE_PIECE_SIZE is already known.
RE_PIECE_SIZE = re.compile(r"piece size\s*\|\s*(?P<piece_size>\d+)")
RE_RUN_SIZE = re.compile(r"run size\s*\|\s*(?P<index_max>\d+)")


def get_index_max(size, piece_size=None):
    piece_size = TREE_PIECE_SIZE if piece_size is None else piece_size
    index_max = (32 * size) + 4
    aux = index_max & ((1 << piece_size) - 1)
    if aux == 0:
        return index_max
    return index_max + (1 << piece_size) - aux


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


def apply_index_max(state, index_max):
    """(Re)derive total_pieces and the tree shape from index_max - the run's
    own ground truth, sourced either from --size or, once it arrives, the
    log's own "run size" line (see RE_RUN_SIZE / the module docstring)."""
    state.total_pieces = index_max // PIECES_PER_LEAF
    state.tree_root, state.tree_by_key = build_tree(index_max)
    if state.tree_root is None:
        state.tree_skipped_reason = (
            f"tree too large to display ({2 * state.total_pieces - 1} nodes > {TREE_NODE_CAP})"
        )


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


_SYSCTL_INT_CACHE = {}


def _sysctl_int(name):
    """sysctl -n <name>, cached: hw.memsize doesn't change during a run."""
    if name in _SYSCTL_INT_CACHE:
        return _SYSCTL_INT_CACHE[name]
    try:
        out = subprocess.run(
            ["sysctl", "-n", name], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True
        )
        val = int(out.stdout.strip()) if out.returncode == 0 else None
    except (OSError, ValueError):
        val = None
    _SYSCTL_INT_CACHE[name] = val
    return val


_VM_STAT_PAGE_SIZE_RE = re.compile(r"page size of (\d+) bytes")
_VM_STAT_FIELD_RE = re.compile(r"^(?P<name>[^:]+):\s*(?P<val>\d+)\.?\s*$")


def get_system_ram():
    """(used_bytes, total_bytes) from macOS vm_stat/sysctl, or (None, total)
    /(None, None) if a piece is unavailable (e.g. not on macOS).

    "used" mirrors Activity Monitor's "Memory Used" - wired + active +
    compressed pages - rather than total-free. That distinction matters here
    because araucaria's big numbers, once past disk_threshold, live in a
    MAP_SHARED mmap backed by an unlinked temp file (see num_create_disk in
    mods/araucaria/lib/num/code.c) rather than the anonymous heap; their
    clean, file-backed pages show up as reclaimable rather than
    wired/active/compressed, so counting them as "used" would overstate real
    memory pressure from this run.
    """
    total = _sysctl_int("hw.memsize")
    try:
        out = subprocess.run(["vm_stat"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return None, total
    if out.returncode != 0:
        return None, total

    text = out.stdout
    m = _VM_STAT_PAGE_SIZE_RE.search(text)
    page_size = int(m.group(1)) if m else 4096

    fields = {}
    for line in text.splitlines():
        m = _VM_STAT_FIELD_RE.match(line)
        if m:
            fields[m.group("name").strip()] = int(m.group("val"))

    wanted = ("Pages wired down", "Pages active", "Pages occupied by compressor")
    if not all(k in fields for k in wanted):
        return None, total
    return sum(fields[k] for k in wanted) * page_size, total


def get_worker_rss(pids):
    """Total resident memory (bytes) across the given pids, via `ps` - not
    virtual size. araucaria's disk-backed big numbers (see get_system_ram
    above) mean a worker's VSZ can dwarf what's actually resident, and its
    RSS can legitimately shrink as the OS pages parts of a big number back
    out to its backing file under memory pressure - that's the disk-backed
    path working as designed, not a leak. Returns None if `ps` itself is
    unavailable, 0 if there are simply no pids to sum."""
    pids = list(pids)
    if not pids:
        return 0
    try:
        out = subprocess.run(
            ["ps", "-o", "rss=", "-p", ",".join(str(p) for p in pids)],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
    except OSError:
        return None
    if out.returncode != 0:
        return None
    total_kb = sum(int(line) for line in out.stdout.split())
    return total_kb * 1024


def fmt_bytes(n):
    if n is None:
        return "?"
    n = float(n)
    for unit in ("B", "K", "M", "G"):
        if n < 1024 or unit == "G":
            return f"{n:.0f}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024


class State:
    def __init__(self, total_pieces=None, n_process=None, explicit_size=None):
        self.start_time = None
        self.last_line_time = None
        self.total_pieces = total_pieces
        self.n_process = n_process
        # --size, if given - kept around so handle_phase can resolve it into
        # index_max the moment TREE_PIECE_SIZE becomes known (the "piece
        # size" log line), rather than only at State construction time.
        self.explicit_size = explicit_size
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
        self.join_events = collections.deque(maxlen=2000)  # (time, dur), node_process's "joined" completions
        # Same (time, dur) pairs, bucketed by the node's depth: a join's cost
        # scales with the size of what it's combining, which (loosely) tracks
        # depth - shallow joins near the root combine much bigger numbers
        # than deep ones - so a single flat average blurs that out. Piece
        # durations shouldn't vary by depth (every piece is the same size),
        # but they're bucketed too for a uniform depth_avg() lookup.
        self.piece_events_by_depth = collections.defaultdict(lambda: collections.deque(maxlen=200))
        self.join_events_by_depth = collections.defaultdict(lambda: collections.deque(maxlen=200))
        # (time, pieces_done + joins_done) once per render tick - the
        # measured, empirical throughput behind measured_eta's global eta,
        # as opposed to the modeled per-task durations above (still used for
        # each tree node's own eta in render_tree). maxlen gives a margin
        # over MEASURED_ETA_WIDE_WINDOW_SECONDS (the longer of the two
        # windows render() uses) worth of ~1/s render ticks, so the oldest
        # sample still within either window is never right at the deque's
        # own eviction boundary.
        self.progress_samples = collections.deque(maxlen=1100)
        self.active = {}  # pid -> {"start", "depth", "i0"}
        self.active_max = 0
        self.phase = "splitting"
        self.phase_start_time = None  # set by set_phase() on every transition away from "splitting"
        self.crashed = collections.deque(maxlen=10)  # (pid, depth, i0, ran_for)
        self.crashed_total = 0
        self.suspected_dead = {}  # pid -> time first seen not-alive
        self.tree_root = None
        self.tree_by_key = None  # (i0, depth) -> TreeNode
        self.tree_skipped_reason = None
        self.process_running = False
        self.process_gone_since = None  # time.time() process_running first went False since done
        self.ever_saw_process = False
        self.done = False

    def touch(self):
        if self.start_time is None:
            self.start_time = time.time()
        self.last_line_time = time.time()

    @property
    def total_joins(self):
        return self.total_pieces - 1 if self.total_pieces else None


def set_phase(state, phase):
    """Record when the run left "splitting" for a later phase - render()
    uses this to show elapsed-in-phase instead of a stale eta once the
    tracked pieces/joins total hits 100% but the process is still running
    (dividing the binary-split result down, then rendering it to decimal -
    real work with no piece/join count of its own to track)."""
    if phase != state.phase:
        state.phase = phase
        state.phase_start_time = time.time()


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
        dur = m.group("dur")
        if dur is not None:
            dur = float(dur)
            state.join_events.append((time.time(), dur))
            state.join_events_by_depth[depth].append((time.time(), dur))
        if tree_node is not None:
            mark_node_done(tree_node)


def handle_piece(state, content):
    m = RE_PIECE.match(content)
    if not m or m.group("action").strip() != "piece":
        return
    dur = float(m.group("dur"))
    state.pieces_done += 1
    state.piece_events.append((time.time(), dur))

    # split_piece's own log line has no depth field (only i0/span) - the
    # "begin" line the same pid logged moments earlier does, so pull it from
    # there. This (and the by-depth bucketing) works even without a
    # materialized tree, since it only needs this pid's own depth.
    entry = state.active.get(int(m.group("pid")))
    depth = entry["depth"] if entry else None
    if depth is not None:
        state.piece_events_by_depth[depth].append((time.time(), dur))

    if state.tree_by_key:
        i0 = int(m.group("i0"))
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
        set_phase(state, action)


def handle_phase(state, content):
    m = RE_PIECE_SIZE.match(content)
    if m:
        # unconditional, unlike run size below: a fresh piece size line
        # arrives on every run (RESTARTED gets a new State but this is a
        # module global), and always reflects the binary that's actually
        # running now.
        apply_piece_size(int(m.group("piece_size")))
        if state.total_pieces is None and state.explicit_size is not None:
            # --size was given but couldn't be resolved into index_max up
            # front (piece_size wasn't known yet) - do it now, first chance.
            apply_index_max(state, get_index_max(state.explicit_size))
        return

    m = RE_RUN_SIZE.match(content)
    if m:
        if state.total_pieces is None:  # else --size already set it explicitly; log agrees, nothing to do
            apply_index_max(state, int(m.group("index_max")))
        return

    m = RE_PHASE.match(content)
    if not m:
        return
    action = m.group("action").strip()
    # "divided"/"pi already stored" only mean the binary-splitting result is
    # ready - pi() still has to run flt_num_display_dec() over it afterward
    # (logged as "display begin"/"display end" under func "pi", below) before
    # the process actually exits, so state.done waits for that instead.
    if action in ("dividing", "divided", "pi already stored", "binary split solved"):
        set_phase(state, action)
    if action == "pi already stored":
        # pi_tree() short-circuits here without ever running the scheduler -
        # the whole result was already on disk from an earlier run.
        if state.total_pieces:
            state.pieces_from_cache += state.total_pieces - state.pieces_done
            state.pieces_done = state.total_pieces
            state.joins_from_cache += state.total_joins - state.joins_done
            state.joins_done = state.total_joins
        if state.tree_root is not None:
            mark_leaves_done(state.tree_root, state.tree_root.leaves_total)
            mark_node_done(state.tree_root)
    elif action == "display begin":
        set_phase(state, "displaying")
    elif action == "display end":
        set_phase(state, "displayed")
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
    "pi": handle_phase,
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


def depth_avg(events_by_depth, depth, fallback):
    """Average duration among samples taken at this exact depth, or
    `fallback` (typically the flat all-depths average for that kind) if this
    depth hasn't been sampled yet - early in a run, or for a lightly
    traveled depth (e.g. near the root, where only one or two nodes ever
    exist at that depth), there just aren't enough samples yet to trust."""
    events = events_by_depth.get(depth) if depth is not None else None
    if events:
        durs = [d for _, d in events]
        if durs:
            return sum(durs) / len(durs)
    return fallback


# how far back the rolling window in measured_eta looks for a throughput
# sample to compare against "now" - long enough to smooth over a single
# scheduling hiccup or a momentary lull, short enough to react within a
# render or two once the run's actual pace changes.
MEASURED_ETA_WINDOW_SECONDS = 300.0

# a second, longer window (3x the short one) - render() calls measured_eta
# with both and shows the spread as a range. Real per-task durations vary a
# lot even for nominally identical work (see measured_eta's docstring), so a
# single point estimate implies more precision than the data supports; the
# gap between a recent-biased and a longer-run-averaged rate is a cheap,
# honest stand-in for that uncertainty.
MEASURED_ETA_WIDE_WINDOW_SECONDS = MEASURED_ETA_WINDOW_SECONDS * 3

# minimum span the rolling window needs before its rate is trusted, so a
# couple of noisy samples right after attach (e.g. the tail() catch-up
# burst - see feed loop below) can't produce a wild estimate; below this,
# render() shows "?" instead.
MEASURED_ETA_MIN_SPAN_SECONDS = 5.0


def measured_eta(state, remaining_units, window_seconds=MEASURED_ETA_WINDOW_SECONDS):
    """Global eta from the run's own observed throughput - how much
    done_units grew over the trailing window, extrapolated forward - rather
    than modeling individual task durations, tree structure, and worker
    count. This naturally reflects however many workers are *actually* busy
    right now, including a tapering-off tail with fewer ready tasks than
    workers, which dividing remaining work by the full configured worker
    count would miss. The cost is a short warm-up (MEASURED_ETA_MIN_SPAN_SECONDS)
    before there's enough history to trust, and no foresight into an
    upcoming run of unusually expensive tasks (e.g. a burst of big joins
    near the root) the way a per-task duration model could offer.

    state.progress_samples is a chronological (time, done_units) deque,
    appended once per render tick by the main loop - not here, so this stays
    a pure read of already-collected history. render() calls this twice,
    once per window length, to show a range instead of one point estimate -
    see MEASURED_ETA_WIDE_WINDOW_SECONDS.
    """
    if remaining_units <= 0:
        return 0.0
    if len(state.progress_samples) < 2:
        return None

    now, done_now = state.progress_samples[-1]
    cutoff = now - window_seconds
    window_start_time, window_start_units = state.progress_samples[0]
    for t, units in state.progress_samples:
        if t >= cutoff:
            window_start_time, window_start_units = t, units
            break

    elapsed = now - window_start_time
    progressed = done_now - window_start_units
    if elapsed < MEASURED_ETA_MIN_SPAN_SECONDS or progressed <= 0:
        return None

    rate = progressed / elapsed
    return remaining_units / rate


def format_eta_range(state, remaining_units):
    """The "eta: ..." line's text: measured_eta from both the short and wide
    windows, shown as a range when they disagree. Real per-task durations
    vary a lot even for nominally identical work (e.g. resource contention
    between worker processes), so a single point estimate implies more
    precision than the underlying data supports - the gap between a
    recent-biased and a longer-run-averaged rate is a cheap, honest stand-in
    for that uncertainty, rather than one number that just happens to be
    wrong by some unstated amount."""
    short = measured_eta(state, remaining_units)
    wide = measured_eta(state, remaining_units, window_seconds=MEASURED_ETA_WIDE_WINDOW_SECONDS)
    etas = [e for e in (short, wide) if e is not None]
    if not etas:
        return "? remaining"
    lo_str, hi_str = fmt_duration(min(etas)), fmt_duration(max(etas))
    if lo_str == hi_str:
        return f"{lo_str} remaining"
    return f"{lo_str}–{hi_str} remaining"


def render_tree(node, lines, now, avg_piece_dur, avg_join_dur, piece_events_by_depth, join_events_by_depth, prefix="", is_last=True, is_root=True):
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
            node_elapsed = now - node.start_time
            label += f" {fmt_duration(node_elapsed)}"
            if node.in_progress:
                # A leaf (leaves_total == 1) is a single split_piece call, so
                # piece durations are the right yardstick; anything else at
                # this exact node is a "joining" combine step. depth_avg
                # prefers samples from this exact depth (join cost scales
                # with what's being combined, which tracks depth) over the
                # flat all-depths average.
                is_leaf = node.leaves_total == 1
                events_by_depth = piece_events_by_depth if is_leaf else join_events_by_depth
                fallback = avg_piece_dur if is_leaf else avg_join_dur
                avg = depth_avg(events_by_depth, node.depth, fallback)
                if avg is not None:
                    remaining = avg - node_elapsed
                    eta_str = fmt_duration(remaining) if remaining > 0 else "any moment"
                    label += f" (eta {eta_str})"
    lines.append(f"{prefix}{connector}{mark} {label}")

    if state in ("done", "pending"):
        return  # collapse: a done subtree needs no detail, a pending one has none yet

    child_prefix = prefix if is_root else prefix + ("   " if is_last else "│  ")
    for i, child in enumerate(node.children):
        render_tree(
            child, lines, now, avg_piece_dur, avg_join_dur,
            piece_events_by_depth, join_events_by_depth,
            child_prefix, i == len(node.children) - 1, is_root=False,
        )


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
    # Same race as sweep_dead's DEAD_GRACE_SECONDS, one level up: the OS can
    # report the whole process gone (pi_process_running's pgrep) an instant
    # before feed_line() has consumed the "display end" log line that sets
    # state.done - so "not running yet" is also what a completely normal
    # finish looks like for a brief moment. Give it the same grace window
    # before showing the "may have crashed" status screen.
    process_gone_recently = (
        state.process_gone_since is not None
        and time.time() - state.process_gone_since < DEAD_GRACE_SECONDS
    )
    if not state.ever_saw_process or (not state.process_running and not state.done and not process_gone_recently):
        return render_status_screen(state)

    lines = []
    lines.append("=== pi_tree dashboard ===  " + time.strftime("%H:%M:%S"))
    lines.append("")

    now = time.time()
    elapsed = now - state.start_time if state.start_time else 0
    lines.append(f"phase:   {state.phase}")
    lines.append(f"elapsed: {fmt_duration(elapsed)} (since dashboard attached)")

    # Both durations come from the C program's own per-event timers, not
    # dashboard wall-clock, so - unlike done_units/elapsed-since-attach -
    # they're unaffected by tail() bursting through a backlog when attaching
    # to a process already mid-run (see the overall-eta comment below).
    # Shared by the stall check, the overall eta, and each tree node's eta.
    piece_durs = [d for _, d in state.piece_events]
    avg_piece_dur = sum(piece_durs) / len(piece_durs) if piece_durs else None
    join_durs = [d for _, d in state.join_events]
    avg_join_dur = sum(join_durs) / len(join_durs) if join_durs else None

    if state.done:
        lines.append("")
        lines.append("*** PROCESS DONE ***")

    if state.last_line_time is not None and not state.done:
        since_last_line = now - state.last_line_time
        # a lull up to one full task duration is normal (e.g. only one slow
        # task left running); only warn once we're well past that.
        stall_threshold = max(60.0, 2 * max(piece_durs)) if piece_durs else 60.0
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
        if not state.done and state.phase == "splitting":
            # Measured from the run's own observed throughput (see
            # measured_eta) rather than modeled from per-task durations and
            # worker count - deliberately NOT based on
            # done_units/elapsed-since-attach directly, since tail() opens
            # the log from byte 0 with no seek-to-end: attaching to a
            # process already mid-run bursts through its whole backlog in a
            # near-instant tight loop before catching up to live, which
            # would spike a naive rate. progress_samples only gets appended
            # once per live render tick (see main()), so that burst is at
            # worst one outlier sample, not the whole basis for the rate.
            lines.append(f"         eta: {format_eta_range(state, total_units - done_units)}")
        elif not state.done:
            # pieces/joins only cover pi_tree()'s own work - the result still
            # has to be divided down and rendered to decimal (dividing/
            # displaying) before the process actually exits, so 100% here
            # doesn't mean done. There's no unit count for that part to base
            # an eta on, so show how long it's taken instead of leaving a
            # stale "00:00 remaining" up once the tracked total is reached.
            phase_elapsed = time.time() - state.phase_start_time if state.phase_start_time else 0
            lines.append(f"         {state.phase}: {fmt_duration(phase_elapsed)} elapsed")
    else:
        # normally just a brief startup gap - the log's own "piece size"/
        # "run size" lines are the first things pi_tree() logs, so this
        # fills in within the first render tick. Stays stuck on "?" for a
        # log from a binary built before those lines existed - --size can't
        # rescue that case either, since it still needs TREE_PIECE_SIZE from
        # the log to resolve into index_max.
        lines.append(f"pieces:  {state.pieces_done} / ?    joins: {state.joins_done} / ? (waiting for the log's \"piece size\"/\"run size\" lines){cache_note}")
    lines.append("")

    n_workers = state.n_process or state.active_max
    crashed_note = f"  ({state.crashed_total} crashed total)" if state.crashed_total else ""
    lines.append(f"active workers: {len(state.active)}/{n_workers or '?'}{crashed_note}")

    ram_used, ram_total = get_system_ram()
    if ram_total is not None:
        ram_line = f"ram: {fmt_bytes(ram_used)} used / {fmt_bytes(ram_total)} total"
        if ram_used is not None:
            ram_line += f" ({100.0 * ram_used / ram_total:4.1f}%)"
        worker_rss = get_worker_rss(state.active.keys())
        if worker_rss:
            ram_line += f"   workers: {fmt_bytes(worker_rss)}"
        lines.append(ram_line)

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
        render_tree(
            state.tree_root, lines, now, avg_piece_dur, avg_join_dur,
            state.piece_events_by_depth, state.join_events_by_depth,
        )
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_path", nargs="?", default=DEFAULT_LOG)
    parser.add_argument("--size", type=int, default=None, help="pi() size argument, to compute total pieces as soon as the log's \"piece size\" line arrives instead of waiting on \"run size\" too")
    parser.add_argument("--n-process", type=int, default=None, help="pi() n_process argument")
    args = parser.parse_args()

    n_process = args.n_process

    def make_state():
        # No total_pieces/tree yet - filled in the moment the log's own
        # "piece size"/"run size" lines arrive (handle_phase ->
        # apply_index_max), which happens on every RESTARTED (run.sh wipes
        # thread_log/ for each new run) since they're the first lines
        # pi_tree() logs. --size (state.explicit_size) lets that happen as
        # soon as piece size is known, without waiting on run size too - but
        # still can't resolve to index_max any earlier than that, since
        # TREE_PIECE_SIZE is a build-time #define with no source we can read
        # it from (see module docstring).
        return State(n_process=n_process, explicit_size=args.size)

    state = make_state()
    done_announced = False

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
                done_announced = False
                continue
            if line is not None:
                feed_line(state, line)

            now = time.time()
            if now - last_render >= 1.0:
                if not state.done:
                    # once done, the OS process is gone for good - no point
                    # spawning a pgrep every second forever just to keep
                    # confirming it's still gone.
                    state.process_running = pi_process_running()
                    if state.process_running:
                        state.ever_saw_process = True
                        state.process_gone_since = None
                    elif state.process_gone_since is None:
                        state.process_gone_since = now
                    sweep_dead(state)
                # once per live tick, not once per log line - see
                # measured_eta, which needs wall-clock-spaced samples, not
                # log-density-spaced ones (a tail() backlog burst would
                # otherwise cram thousands of samples into a few ms).
                state.progress_samples.append((now, state.pieces_done + state.joins_done))
                draw(state)
                last_render = now

            if state.done and state.ever_saw_process and not done_announced:
                # re-draw unconditionally: the periodic draw() above is
                # throttled to once/sec, so the frame the user was looking
                # at when the run finished may predate state.done and still
                # be missing the "PROCESS DONE" banner render() adds for it.
                # Keep looping afterward instead of exiting - the finished
                # dashboard (full stats included) stays on screen until the
                # user closes the terminal or hits Ctrl+C themselves.
                draw(state)
                done_announced = True
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
