#!/usr/bin/env python3
"""Live terminal dashboard for a pi_tree run.

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

TREE_PIECE_SIZE = None
PIECES_PER_LEAF = None


def apply_piece_size(piece_size):
    global TREE_PIECE_SIZE, PIECES_PER_LEAF
    TREE_PIECE_SIZE = piece_size
    PIECES_PER_LEAF = 1 << piece_size

_TASK_ID = r"\[\s*(?P<idx>\d+)\]\[\s*(?P<pid>\d+)\]"

RE_NODE_PROCESS = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|\s*"
    r"(?P<i0>\d+)\s+(?P<n2>\d+)\s+(?P<depth>\d+)"
    r"(?:\s*\|\s*(?:(?P<dur>[\d.]+)|avg\s+(?P<mem>\d+)B))?"
)
RE_PIECE = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|\s*(?P<i0>\d+)\s+(?P<span>\d+)\s*\|\s*(?P<dur>[\d.]+)"
)
RE_TASK_START = re.compile(_TASK_ID + r"\s*(?P<action>.+?)\s*\|")
RE_TASK_END = re.compile(_TASK_ID + r"\s*(?P<action>.+?)\s*\|(?:.*\|)?\s*(?P<dur>[\d.]+)\s*$")
RE_SCHEDULER = re.compile(r"(?P<action>.+?)\s*\|\s*(?P<val>\d+)")
RE_PHASE = re.compile(r"(?P<action>.+?)\s*\|(?:\s*(?P<dur>[\d.]+))?")

RE_PIECE_SIZE = re.compile(r"piece size\s*\|\s*(?P<piece_size>\d+)")
RE_RUN_SIZE = re.compile(r"run size\s*\|\s*(?P<index_max>\d+)")


def get_index_max(size, piece_size=None):
    piece_size = TREE_PIECE_SIZE if piece_size is None else piece_size
    index_max = (32 * size) + 4
    aux = index_max & ((1 << piece_size) - 1)
    if aux == 0:
        return index_max
    return index_max + (1 << piece_size) - aux


SPAN_VS_REMAINDER_CUTOFF = 64


def leaves_covered(n2):
    if n2 > SPAN_VS_REMAINDER_CUTOFF:
        return n2 // PIECES_PER_LEAF
    return 1 << (n2 - TREE_PIECE_SIZE)


TREE_NODE_CAP = 20000  # total nodes; skip the view rather than choke on it


class TreeNode:
    __slots__ = (
        "i0", "depth", "kind", "n2", "leaves_total", "leaves_done",
        "own_done", "in_progress", "task_idx", "pid", "start_time", "active_count", "parent", "children",
        "mem_estimate",
    )

    def __init__(self, i0, depth, kind, n2, parent):
        self.i0 = i0
        self.depth = depth
        self.kind = kind  # "BIG" or "SPAN"
        self.n2 = n2  # remainder (BIG) or span (SPAN)
        self.leaves_total = (n2 // PIECES_PER_LEAF) if kind == "BIG" else (1 << (n2 - TREE_PIECE_SIZE))
        self.leaves_done = 0
        self.own_done = False
        self.in_progress = False
        self.task_idx = None
        self.pid = None
        self.start_time = None
        self.active_count = 0
        self.parent = parent
        self.children = []
        self.mem_estimate = None  # bytes, set when a "joining" line is seen for this node


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
    total_pieces = index_max // PIECES_PER_LEAF
    if 2 * total_pieces - 1 > TREE_NODE_CAP:
        return None, None
    by_key = {}
    root = _build_big(1, index_max, 0, None, by_key)
    return root, by_key


def apply_index_max(state, index_max):
    state.total_pieces = index_max // PIECES_PER_LEAF
    state.tree_root, state.tree_by_key = build_tree(index_max)
    if state.tree_root is None:
        state.tree_skipped_reason = (
            f"tree too large to display ({2 * state.total_pieces - 1} nodes > {TREE_NODE_CAP})"
        )


def mark_leaves_done(node, leaves):
    n = node
    while n is not None:
        n.leaves_done = min(n.leaves_done + leaves, n.leaves_total)
        n = n.parent


def mark_active(node, delta):
    n = node
    while n is not None:
        n.active_count += delta
        n = n.parent


def mark_node_done(node):
    node.own_done = True
    node.in_progress = False
    node.task_idx = None
    node.pid = None
    node.start_time = None
    mark_active(node, -1)


def mark_node_done_from_cache(node):
    """Like mark_node_done(), but for a node that was never marked active
    (no "begin" line seen) - e.g. a fully-cached run where pi_tree() finds
    the result already stored and returns before the scheduler runs."""
    node.own_done = True
    node.in_progress = False
    node.task_idx = None
    node.pid = None
    node.start_time = None


def pi_process_running():
    try:
        result = subprocess.run(
            ["pgrep", "-f", r"src/(main|debug)\.o"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return result.returncode == 0
    except OSError:
        return False


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

_MEMINFO_FIELD_RE = re.compile(r"^(?P<name>\S+):\s*(?P<val>\d+)\s*kB\s*$")


def _get_system_ram_linux():
    try:
        with open("/proc/meminfo") as f:
            text = f.read()
    except OSError:
        return None, None

    fields = {}
    for line in text.splitlines():
        m = _MEMINFO_FIELD_RE.match(line)
        if m:
            fields[m.group("name")] = int(m.group("val")) * 1024

    total = fields.get("MemTotal")
    if total is None:
        return None, None
    available = fields.get("MemAvailable")
    used = total - available if available is not None else None
    return used, total


def _get_system_ram_macos():
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


def get_system_ram():
    if sys.platform.startswith("linux"):
        return _get_system_ram_linux()
    return _get_system_ram_macos()


def get_pid_rss(pids):
    """pid -> RSS bytes, for the given live pids (missing/dead pids are omitted)."""
    pids = list(pids)
    if not pids:
        return {}
    try:
        out = subprocess.run(
            ["ps", "-o", "pid=,rss=", "-p", ",".join(str(p) for p in pids)],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
    except OSError:
        return {}
    if out.returncode != 0:
        return {}
    result = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            pid, rss = parts
            result[int(pid)] = int(rss) * 1024
    return result


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
        self.explicit_size = explicit_size
        self.pieces_done = 0
        self.pieces_from_cache = 0
        self.joins_done = 0
        self.joins_from_cache = 0
        self.piece_events = collections.deque(maxlen=2000)  # (time, dur)
        self.join_events = collections.deque(maxlen=2000)  # (time, dur)
        self.piece_events_by_depth = collections.defaultdict(lambda: collections.deque(maxlen=200))
        self.join_events_by_depth = collections.defaultdict(lambda: collections.deque(maxlen=200))
        self.progress_samples = collections.deque(maxlen=1100)
        self.active = {}  # pid -> {"start", "depth", "i0"}
        self.active_max = 0
        self.phase = "splitting"
        self.phase_start_time = None  # set by set_phase() on every transition away from "splitting"
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
    if phase != state.phase:
        state.phase = phase
        state.phase_start_time = time.time()


def handle_node_process(state, content):
    m = RE_NODE_PROCESS.match(content)
    if not m:
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
            tree_node.pid = pid
            tree_node.start_time = entry["start"]
            mark_active(tree_node, 1)
    elif action == "already stored":
        state.active.pop(pid, None)
        covered = leaves_covered(int(m.group("n2")))
        state.pieces_done += covered
        state.pieces_from_cache += covered
        joins_covered = covered - 1
        state.joins_done += joins_covered
        state.joins_from_cache += joins_covered
        if tree_node is not None:
            mark_leaves_done(tree_node, covered)
            mark_node_done(tree_node)
    elif action == "joining":
        mem = m.group("mem")
        if tree_node is not None and mem is not None:
            tree_node.mem_estimate = int(mem)
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

    entry = state.active.get(int(m.group("pid")))
    depth = entry["depth"] if entry else None
    if depth is not None:
        state.piece_events_by_depth[depth].append((time.time(), dur))

    if state.tree_by_key:
        i0 = int(m.group("i0"))
        tree_node = state.tree_by_key.get((i0, depth)) if depth is not None else None
        if tree_node is not None:
            mark_leaves_done(tree_node, 1)
            mark_node_done(tree_node)


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
        apply_piece_size(int(m.group("piece_size")))
        if state.total_pieces is None and state.explicit_size is not None:
            apply_index_max(state, get_index_max(state.explicit_size))
        return

    m = RE_RUN_SIZE.match(content)
    if m:
        if state.total_pieces is None:
            apply_index_max(state, int(m.group("index_max")))
        return

    m = RE_PHASE.match(content)
    if not m:
        return
    action = m.group("action").strip()
    if action in ("dividing", "divided", "pi already stored", "binary split solved"):
        set_phase(state, action)
    if action == "pi already stored":
        if state.total_pieces:
            state.pieces_from_cache += state.total_pieces - state.pieces_done
            state.pieces_done = state.total_pieces
            state.joins_from_cache += state.total_joins - state.joins_done
            state.joins_done = state.total_joins
        if state.tree_root is not None:
            mark_leaves_done(state.tree_root, state.tree_root.leaves_total)
            mark_node_done_from_cache(state.tree_root)
    elif action == "display begin":
        set_phase(state, "displaying")
    elif action == "display end":
        set_phase(state, "displayed")
        state.done = True


def handle_untracked(state, content):
    """pi_big()'s own split_span/split_big recursion (lib/big/code.c) logs in a
    format this dashboard doesn't parse (no [idx][pid] task id - it isn't
    forked per task like pi_tree()'s scheduler). It's currently dead code
    (only pi_tree() is called from src/main.c); this entry exists so a future
    switch back to pi_big() shows nothing tracked instead of being silently
    indistinguishable from a missing DISPATCH entry."""
    del state, content


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
    "split_span": handle_untracked,
    "split_big": handle_untracked,
}


def feed_line(state, line):
    line = line.rstrip("\n")
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


DEAD_GRACE_SECONDS = 3.0


def fmt_duration(seconds):
    if seconds is None or seconds != seconds or seconds == float("inf"):
        return "?"
    seconds = max(0, int(seconds))
    h, rem = divmod(seconds, 3600)
    m, s = divmod(rem, 60)
    if h:
        return f"{h:d}:{m:02d}:{s:02d}"
    return f"{m:02d}:{s:02d}"


def depth_avg(events_by_depth, depth, fallback):
    events = events_by_depth.get(depth) if depth is not None else None
    if events:
        durs = [d for _, d in events]
        if durs:
            return sum(durs) / len(durs)
    return fallback


MEASURED_ETA_WINDOW_SECONDS = 300.0
MEASURED_ETA_WIDE_WINDOW_SECONDS = MEASURED_ETA_WINDOW_SECONDS * 3
MEASURED_ETA_MIN_SPAN_SECONDS = 5.0


def measured_eta(state, remaining_units, window_seconds=MEASURED_ETA_WINDOW_SECONDS):
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


def active_mem_estimate(node):
    if node is None or node.own_done:
        return 0
    total = node.mem_estimate if (node.in_progress and node.mem_estimate is not None) else 0
    for child in node.children:
        total += active_mem_estimate(child)
    return total


def format_eta_range(state, remaining_units):
    short = measured_eta(state, remaining_units)
    wide = measured_eta(state, remaining_units, window_seconds=MEASURED_ETA_WIDE_WINDOW_SECONDS)
    etas = [e for e in (short, wide) if e is not None]
    if not etas:
        return "? remaining"
    lo_str, hi_str = fmt_duration(min(etas)), fmt_duration(max(etas))
    if lo_str == hi_str:
        return f"{lo_str} remaining"
    return f"{lo_str}–{hi_str} remaining"


def render_tree(node, lines, now, avg_piece_dur, avg_join_dur, piece_events_by_depth, join_events_by_depth, pid_rss, prefix="", is_last=True, is_root=True):
    if node.own_done:
        mark, state, status = "✓", "done", None
    elif node.in_progress:
        mark, state, status = "▸", "in_progress", str(node.task_idx)
    elif node.leaves_done > 0:
        mark, state, status = "·", "in_progress", None
    elif node.active_count > 0:
        mark, state, status = "○", "in_progress", None
    else:
        mark, state, status = "·", "pending", None

    connector = "" if is_root else ("└─ " if is_last else "├─ ")
    if node.kind == "BIG":
        label = f"[{node.depth}, B, {node.i0:,}]"
    else:
        label = f"[{node.depth}, {node.n2}, {node.i0:,}]"
    if status is not None:
        label += f" [{status}]"
        if node.start_time is not None:
            node_elapsed = now - node.start_time
            label += f" {fmt_duration(node_elapsed)}"
            if node.in_progress:
                is_leaf = node.leaves_total == 1
                events_by_depth = piece_events_by_depth if is_leaf else join_events_by_depth
                fallback = avg_piece_dur if is_leaf else avg_join_dur
                avg = depth_avg(events_by_depth, node.depth, fallback)
                if avg is not None:
                    remaining = avg - node_elapsed
                    eta_str = fmt_duration(remaining) if remaining > 0 else "any moment"
                    label += f" (eta {eta_str})"
                current = pid_rss.get(node.pid) if node.pid is not None else None
                if node.mem_estimate is not None or current is not None:
                    est_str = fmt_bytes(node.mem_estimate) if node.mem_estimate is not None else "?"
                    cur_str = fmt_bytes(current) if current is not None else "?"
                    label += f" {est_str} | {cur_str}"
    lines.append(f"{prefix}{connector}{mark} {label}")

    if state in ("done", "pending"):
        return

    child_prefix = prefix if is_root else prefix + ("   " if is_last else "│  ")
    for i, child in enumerate(node.children):
        render_tree(
            child, lines, now, avg_piece_dur, avg_join_dur,
            piece_events_by_depth, join_events_by_depth, pid_rss,
            child_prefix, i == len(node.children) - 1, is_root=False,
        )


def render_status_screen(state):
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
        lines.append("  (check thread_log/run.log for the last lines)")
    return "\n".join(lines)


def render(state):
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

    piece_durs = [d for _, d in state.piece_events]
    avg_piece_dur = sum(piece_durs) / len(piece_durs) if piece_durs else None
    join_durs = [d for _, d in state.join_events]
    avg_join_dur = sum(join_durs) / len(join_durs) if join_durs else None

    if state.done:
        lines.append("")
        lines.append("*** PROCESS DONE ***")

    if state.last_line_time is not None and not state.done:
        since_last_line = now - state.last_line_time
        stall_threshold = max(60.0, 2 * max(piece_durs)) if piece_durs else 60.0
        if since_last_line > stall_threshold:
            lines.append(
                f"WARNING: no log activity for {fmt_duration(since_last_line)} "
                "- the run may have stopped"
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
            eta_line = f"         eta: {format_eta_range(state, total_units - done_units)}"
            if state.tree_root is not None:
                eta_line += f"    est. memory: {fmt_bytes(active_mem_estimate(state.tree_root))}"
            lines.append(eta_line)
        elif not state.done:
            phase_elapsed = time.time() - state.phase_start_time if state.phase_start_time else 0
            lines.append(f"         {state.phase}: {fmt_duration(phase_elapsed)} elapsed")
    else:
        lines.append(f"pieces:  {state.pieces_done} / ?    joins: {state.joins_done} / ? (waiting for the log's \"piece size\"/\"run size\" lines){cache_note}")
    lines.append("")

    n_workers = state.n_process or state.active_max
    lines.append(f"active workers: {len(state.active)}/{n_workers or '?'}")

    ram_used, ram_total = get_system_ram()
    ram_parts = []
    if ram_total is not None:
        ram_part = f"ram: {fmt_bytes(ram_used)} used / {fmt_bytes(ram_total)} total"
        if ram_used is not None:
            ram_part += f" ({100.0 * ram_used / ram_total:4.1f}%)"
        ram_parts.append(ram_part)
    pid_rss = get_pid_rss(state.active.keys())
    if pid_rss:
        ram_parts.append(f"workers: {fmt_bytes(sum(pid_rss.values()))}")
    if ram_parts:
        lines.append("   ".join(ram_parts))

    lines.append("")
    if state.tree_root is not None:
        lines.append("tree (finished/pending subtrees collapsed):")
        render_tree(
            state.tree_root, lines, now, avg_piece_dur, avg_join_dur,
            state.piece_events_by_depth, state.join_events_by_depth, pid_rss,
        )
    elif state.tree_skipped_reason:
        lines.append(f"tree: {state.tree_skipped_reason}")

    return "\n".join(lines)


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
        return State(n_process=n_process, explicit_size=args.size)

    state = make_state()
    done_announced = False

    sys.stdout.write("\x1b[?1049h\x1b[?25l\x1b[48;2;10;20;60m")
    sys.stdout.flush()
    try:
        last_render = 0.0
        for line in tail(args.log_path):
            if line is RESTARTED:
                state = make_state()
                done_announced = False
                continue
            if line is not None:
                feed_line(state, line)

            now = time.time()
            if now - last_render >= 1.0:
                if not state.done:
                    state.process_running = pi_process_running()
                    if state.process_running:
                        state.ever_saw_process = True
                        state.process_gone_since = None
                    elif state.process_gone_since is None:
                        state.process_gone_since = now
                state.progress_samples.append((now, state.pieces_done + state.joins_done))
                draw(state)
                last_render = now

            if state.done and state.ever_saw_process and not done_announced:
                draw(state)
                done_announced = True
    finally:
        sys.stdout.write("\x1b[0m\x1b[?25h\x1b[?1049l")
        sys.stdout.flush()


def _raise_keyboard_interrupt(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, _raise_keyboard_interrupt)
    try:
        main()
    except KeyboardInterrupt:
        pass
