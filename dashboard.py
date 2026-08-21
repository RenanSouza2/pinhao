#!/usr/bin/env python3
"""Live terminal dashboard for a pi_tree run.

Usage: ./dashboard.py [path/to/run.log] [--size N] [--n-process N]
"""

import argparse
import collections
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import termios
import time
import tty

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LOG = os.path.join(REPO_ROOT, "thread_log", "run.log")
CACHE_DIR = os.path.join(REPO_ROOT, "cache")

TREE_PIECE_SIZE = None
PIECES_PER_LEAF = None

# Muted amber (#C9A66B) for the "loading"/"writing" micro-phases (actively
# blocked in the I/O syscall). Not a full \x1b[0m reset: the screen
# background is set once (SGR 48) when the alt-screen is entered and never
# reapplied per frame, so a bare reset here would strip it for the rest of
# the run. 39 (default foreground) clears just the color this pair set,
# leaving the background alone.
IO_ATTN_ON = "\x1b[38;2;201;166;107m"
IO_ATTN_OFF = "\x1b[39m"

# Wine (#722F37), not vibrant red, to flag the "locking" micro-phase. 39
# (default foreground) is enough to clear it - see IO_ATTN_OFF comment
# above for why a bare reset is avoided.
LOCK_ON = "\x1b[38;2;114;47;55m"
LOCK_OFF = "\x1b[39m"

# Sage (#6B8E6B), a muted green rather than a vibrant one, for the
# "multiplying" micro-phase - same 39-only reset as LOCK_OFF.
MUL_ON = "\x1b[38;2;107;142;107m"
MUL_OFF = "\x1b[39m"

# Tree node states on two axes. Hue says what kind of node it is - green for
# work behind you, violet for work still ahead, sky for work available now,
# muted blue for the trail down to a running worker, and MUL_ON's green on the
# node that is running - the same green its "multiplying" phase is written in,
# so the node and the work it is doing read as one thing.
#
# Brightness says how much to care, and ready deliberately outranks the trail:
# a node waiting only on a free slot is the next thing that will happen, while
# an ancestor of a running one is just a breadcrumb. Ready and the trail share
# a family, so they are held far apart within it - saturated sky against a
# desaturated periwinkle. At the other end NODE_DONE and NODE_PENDING are
# within 0.03 of each other in contrast against the #0A143C ground, too close
# to tell apart by weight - hue alone separates them, so neither has to get
# loud and the boundary between the two draws the frontier of the computation.
# A ready node dims to NODE_STARVED while the scheduler has no slot to give it,
# so the whole launch queue dims when the machine is full and brightens the
# moment something frees up. Cleared with 39 (default foreground) rather than
# a bare reset - see IO_ATTN_OFF above for why.
NODE_RUNNING = "\x1b[38;2;107;142;107m"  # #6B8E6B  green,    contrast  4.84
NODE_READY = "\x1b[38;2;99;180;228m"     # #63B4E4  sky,                7.79
NODE_ACTIVE = "\x1b[38;2;142;156;200m"   # #8E9CC8  blue,               6.57
NODE_DONE = "\x1b[38;2;47;92;74m"        # #2F5C4A  green,              2.33
NODE_STARVED = "\x1b[38;2;154;127;192m"  # #9A7FC0  violet,             5.24
NODE_PENDING = "\x1b[38;2;78;63;99m"     # #4E3F63  violet,             1.88
NODE_OFF = "\x1b[39m"

# Bold teal (#4FB3A5) on the thread badge of a task granted more than one
# thread. The badge is only printed in that case, so any badge at all is the
# signal and the color is what makes it carry across a full tree. Cleared
# with 22;39 (normal intensity + default foreground) rather than a bare
# reset - see IO_ATTN_OFF above for why.
# Coral (#E07A5F) for the few states that mean something is going wrong: the
# run has gone quiet, the scheduler has stopped admitting work, or the disk
# lock is contended more often than not. Spent sparingly on purpose - a colour
# that is usually absent carries far more than one that is always on screen.
# Cleared with 22;39 (normal intensity + default foreground) since it is bold.
ALERT_ON = "\x1b[1;38;2;224;122;95m"
ALERT_OFF = "\x1b[22;39m"

# Neutral grey (#7C808C) for a task's measured RSS, set against the estimate
# beside it in the default foreground: the estimate is what the scheduler acts
# on, the measurement is the check on it, so only one of the pair reads loud.
RSS_ON = "\x1b[38;2;124;128;140m"
RSS_OFF = "\x1b[39m"

# Cyan (#4396A2) on a task holding more than one thread slot. Flat across every
# width: the badge says a task is wide, the number beside it says how wide.
# The only cyan on the dashboard, and cool on purpose - every warm hue here
# already means something is wrong (coral alert, wine lock, amber I/O), so a
# warm badge would read as a condition rather than as a count.
MULTI_THR_ON = "\x1b[38;2;67;150;162m"
MULTI_THR_OFF = "\x1b[39m"

ANSI_SGR_RE = re.compile(r"\x1b\[[0-9;]*m")

# Bar segments, heaviest to lightest: in use, held but not working, nothing.
BAR_FULL = "\u2588"
BAR_HELD = "\u2591"
BAR_NONE = " "

# Slate (#6E80B8) over the whole bar, the same tone a done node's id gets: a
# full-width run of \u2588 in the default foreground is the highest-contrast shape
# on the screen, which puts the loudest element on the least surprising state.
# Cleared with 39 (default foreground) rather than a bare reset - see
# IO_ATTN_OFF above for why.
BAR_ON = "\x1b[38;2;110;128;184m"
BAR_OFF = "\x1b[39m"

# The hard-limit mark on a bar, in the coral of ALERT_ON without its bold
# weight: a bar is already a loud shape, so the tone alone carries it.
BAR_ALERT = "\x1b[38;2;224;122;95m"

# Marks laid on a bar: a dotted rule for a threshold, a half cell for a
# measured value. Shape says which kind of reading it is, so colour is left
# free to say how hard the threshold is.
BAR_LIMIT = "\u2506"
BAR_MEASURED = "\u258c"

# The progress cursor pulses in length as log lines arrive and rests at full
# height once they stop. Stepped by log arrivals, never by the redraw clock:
# an animation tied to frames keeps moving right through a stall, which is
# exactly when it must not. Oscillating rather than cycling - \u2577 back to \u2575
# would read as the mark hopping, not as one mark breathing.
CURSOR_PULSE = ("\u2575", "\u2502", "\u2577", "\u2502")
CURSOR_REST = "\u2502"

# Ramp for a bar whose value is itself the worry: calm, noticing, worried, bad.
# Interpolated rather than stepped, so a ratio creeping up drifts in hue instead
# of jumping a threshold - the movement is the signal, not the crossing.
# Biased cool: the lower half of any reading is fine, so green holds to 0.50
# and the whole warm escalation happens across the top half. A ramp that starts
# warming immediately spends its loudest tones on ordinary conditions.
SEVERITY_STOPS = (
    (0.00, (107, 142, 107)),  # sage, as MUL_ON
    (0.50, (122, 146, 102)),  # sage, barely warmed - still reads green
    (0.70, (201, 166, 107)),  # amber, as IO_ATTN_ON
    (0.85, (224, 122, 95)),   # coral, as ALERT_ON
    (1.00, (196, 80, 79)),    # deep red
)


def severity_colour(frac, calm=0.0, alarm=1.0):
    """`calm` and `alarm` remap the reading before it hits the ramp: at or below
    calm it takes the coolest tone, at or above alarm the hottest. One ramp then
    serves measures whose comfortable range differs - a 35% lock miss ratio is
    already worth noticing, 35% memory use is not."""
    span = alarm - calm
    frac = 0.0 if span <= 0 else (frac - calm) / span
    frac = min(max(frac, 0.0), 1.0)
    for (lo, c_lo), (hi, c_hi) in zip(SEVERITY_STOPS, SEVERITY_STOPS[1:]):
        if frac <= hi:
            t = 0.0 if hi == lo else (frac - lo) / (hi - lo)
            r, g, b = (round(a + (z - a) * t) for a, z in zip(c_lo, c_hi))
            return f"\x1b[38;2;{r};{g};{b}m"
    r, g, b = SEVERITY_STOPS[-1][1]
    return f"\x1b[38;2;{r};{g};{b}m"


def apply_piece_size(piece_size):
    global TREE_PIECE_SIZE, PIECES_PER_LEAF
    TREE_PIECE_SIZE = piece_size
    PIECES_PER_LEAF = 1 << piece_size

_TASK_ID = r"\[\s*(?P<idx>\d+)\]\[\s*(?P<pid>\d+)\]\[\s*(?P<ts>[\d.]+)\]"

RE_NODE_PROCESS = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|\s*"
    r"(?P<i0>\d+)\s+(?P<n2>\d+)\s+(?P<depth>\d+)"
    r"(?:\s*\|\s*(?:(?P<dur>[\d.]+)|avg\s+(?P<mem>\d+)B)(?:\s+(?P<lock>HIT|MISS))?)?"
)
RE_PIECE = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|\s*(?P<i0>\d+)\s+(?P<span>\d+)\s*\|\s*(?P<dur>[\d.]+)"
)
# SUM (the scheduler's running total at launch) is matched but not captured:
# the total shown is rebuilt from the per-task THR counts, which also fall as
# tasks end, where SUM only ever states the value at one launch.
# THR/SUM and MEM are both optional: older builds log neither.
RE_TASK_START = re.compile(
    _TASK_ID + r"\s*(?P<action>.+?)\s*\|"
    r"(?:\s*THR\s+(?P<thr>\d+)\s+SUM\s+\d+(?:\s+MEM\s+(?P<mem>\d+))?)?"
)
RE_TASK_END = re.compile(_TASK_ID)
RE_SCHEDULER = re.compile(r"(?P<action>.+?)\s*\|\s*(?P<val>\d+)")
# The leading [ts] is optional: the config lines it shares this shape with
# don't carry one, nor do logs from a build predating it.
RE_PHASE = re.compile(r"(?:\[\s*(?P<ts>[\d.]+)\]\s*)?(?P<action>.+?)\s*\|(?:\s*(?P<dur>[\d.]+))?")

# pi_tree's run configuration, all logged as "<label> | <int>".
RE_CONFIG = re.compile(
    r"(?P<name>piece size|run size|n process|mem launch|mem max|disk lock)\s*\|\s*(?P<val>\d+)"
)

# Leading task id of any per-task line, used only to advance the thread-time
# accumulator to the newest log timestamp (see thread_tick).
RE_TS = re.compile(_TASK_ID)


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

# Where system memory stops being comfortable and starts being the thing that
# ends the run: below RAM_CALM there is room for the page cache as well as the
# workers, by RAM_ALARM there is neither and the workers are being swapped.
# Pulled below the ramp's own cool bias on purpose: memory is the measure that
# ends runs, and it did so last at ~75% and climbing. These bounds put amber
# near 78% of the machine and red at 95%.
RAM_CALM = 0.35
RAM_ALARM = 0.95


class TreeNode:
    __slots__ = (
        "i0", "depth", "kind", "n2", "leaves_total", "leaves_done",
        "own_done", "in_progress", "task_idx", "pid", "threads", "start_time", "active_count", "parent", "children",
        "mem_estimate", "term", "micro",
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
        self.threads = None  # threads this node's task was granted, from its "task start" line
        self.start_time = None
        self.active_count = 0
        self.parent = parent
        self.children = []
        self.mem_estimate = None  # bytes the scheduler booked, from "task start" MEM
        self.term = None  # e.g. "P1xP2", from a join's "mul ..." header line; leaves have none
        self.micro = None  # e.g. "loading P1" / "multiplying" / "evaluating", from a phase line


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


def mark_node_done(node, was_active=True):
    """was_active=False for a node that was never marked active (no "begin"
    line seen) - e.g. a fully-cached run where pi_tree() finds the result
    already stored and returns before the scheduler runs."""
    node.own_done = True
    node.in_progress = False
    node.task_idx = None
    node.pid = None
    node.threads = None
    node.start_time = None
    node.term = None
    node.micro = None
    if was_active:
        mark_active(node, -1)


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


def get_run_start_time():
    """Wall-clock start time of the running pi process, taken from the oldest
    pid matching the run: workers are fork()ed (not exec'd) from the same
    binary, so they all share its command line and pgrep -f matches every
    one of them - the oldest pid is the original parent."""
    try:
        result = subprocess.run(
            ["pgrep", "-f", r"src/(main|debug)\.o"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    pids = [p for p in result.stdout.split() if p]
    if not pids:
        return None
    try:
        out = subprocess.run(
            ["ps", "-o", "etimes=", "-p", ",".join(pids)],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
    except OSError:
        return None
    if out.returncode != 0:
        return None
    etimes = [int(x) for x in out.stdout.split() if x.strip()]
    if not etimes:
        return None
    return time.time() - max(etimes)


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


def get_dir_size(path):
    """Sum of file sizes under path, recursing into subdirectories. Returns
    None if path doesn't exist (e.g. cache/ layout changed)."""
    total = 0
    try:
        with os.scandir(path) as it:
            for entry in it:
                try:
                    if entry.is_dir(follow_symlinks=False):
                        sub = get_dir_size(entry.path)
                        if sub is not None:
                            total += sub
                    else:
                        total += entry.stat(follow_symlinks=False).st_size
                except OSError:
                    continue
    except OSError:
        return None
    return total


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
        self.lines_seen = 0
        self.cursor_lines = 0   # lines_seen when the cursor last stepped
        self.cursor_phase = 0   # index into CURSOR_PULSE
        self.total_pieces = total_pieces
        self.n_process = n_process  # from --n-process
        self.n_process_logged = None  # from the log's "n process" line, after pi_tree's clamp to core count
        self.explicit_size = explicit_size
        self.mem_launch = None  # new tasks launch only while usage is below this
        self.mem_max = None  # a launched task may overshoot up to here, never past
        self.disk_lock_enabled = None  # None until the log's "disk lock" line arrives
        self.pieces_done = 0
        self.joins_done = 0
        self.piece_events = collections.deque(maxlen=2000)  # durations, seconds
        self.join_events = collections.deque(maxlen=2000)  # durations, seconds
        self.piece_events_by_depth = collections.defaultdict(lambda: collections.deque(maxlen=200))
        self.join_events_by_depth = collections.defaultdict(lambda: collections.deque(maxlen=200))
        self.lock_requests = 0  # "locked" lines seen, across all workers
        self.lock_misses = 0  # of those, the ones that found the lock already held
        self.lock_tokens_seen = False  # a "locked" line carried HIT/MISS (older builds log neither)
        self.locking_pids = set()  # pids currently blocked between "locking" and "locked"
        self.active = {}  # pid -> {"start", "depth", "i0", "threads"}
        self.active_max = 0
        self.threads_seen = False  # a "task start" line carried THR/SUM (older builds don't log it)
        self.threads_reported = None  # scheduler's own "active threads" total, used until THR arrives
        self.thread_time = 0.0  # thread-seconds, in log time, for the utilization average
        self.thread_span = 0.0  # seconds those thread-seconds cover
        self.thread_last_ts = None  # log timestamp the two above are folded up to
        self.phase = "splitting"
        self.phase_start_time = None  # set by set_phase() on every transition away from "splitting"
        self.tree_root = None
        self.tree_by_key = None  # (i0, depth) -> TreeNode
        self.tree_skipped_reason = None
        self.process_running = False
        self.process_gone_since = None  # time.time() process_running first went False since done
        self.ever_saw_process = False
        self.run_start_time = None  # wall-clock start of the actual pi process, from get_run_start_time()
        self.done = False

    def touch(self):
        if self.start_time is None:
            self.start_time = time.time()
        self.last_line_time = time.time()
        self.lines_seen += 1

    @property
    def total_joins(self):
        return self.total_pieces - 1 if self.total_pieces else None


def active_threads(state):
    """Threads currently booked by running tasks. Falls back to the
    scheduler's own "active threads" line while no "task start" line has
    carried THR yet - either the dashboard attached mid-run, or the run comes
    from a build that doesn't log it."""
    if not state.threads_seen:
        return state.threads_reported
    return sum(entry.get("threads") or 0 for entry in state.active.values())


def thread_budget(state):
    """The run's thread ceiling: n_process, from the log's own header line or
    from --n-process. pi_tree logs it on every run, so this is None only for a
    log from an older build."""
    return state.n_process_logged or state.n_process


def thread_tick(state, ts):
    """Fold the thread total up to log timestamp ts into the utilization
    accumulator. Called before every state change, so the total folded in is
    the one that held over the interval just closed. Log timestamps, not wall
    clock, so replaying a finished log gives the same average as watching it
    live.

    The cursor only ever moves forward: every worker writes to the one log and
    stamps its line before the write, so lines interleave out of order. Letting
    ts move the cursor back would fold the stretch between it and the newer
    timestamp in twice, at two different thread totals."""
    if not state.threads_seen:
        return
    if ts > state.thread_last_ts:
        dt = ts - state.thread_last_ts
        state.thread_time += (working_threads(state) or 0) * dt
        state.thread_span += dt
        state.thread_last_ts = ts


def attach_task_plan(state, pid, tree_node):
    """A task's plan (threads, booked memory) and its node identity arrive on
    two separate lines ("task start" and "begin") whose order isn't guaranteed -
    the child logs "begin" as soon as it's forked, the parent logs "task start"
    after. Both sides call this, so whichever lands second attaches the plan."""
    if tree_node is None:
        return
    entry = state.active.get(pid)
    if entry is None:
        return
    if entry.get("threads") is not None:
        tree_node.threads = entry["threads"]
    if entry.get("mem") is not None:
        tree_node.mem_estimate = entry["mem"]


def set_phase(state, phase, ts=None):
    """ts is the phase line's own CLOCK_REALTIME stamp, which is the same epoch
    as time.time(). Without it the timer would restart whenever the dashboard
    attaches, reading 00:00 on a phase that began an hour ago."""
    if phase != state.phase:
        state.phase = phase
        state.phase_start_time = float(ts) if ts is not None else time.time()


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
        ts = float(m.group("ts"))
        entry = state.active.setdefault(pid, {})
        entry["start"] = ts
        entry["depth"] = depth
        entry["i0"] = i0
        if tree_node is not None:
            tree_node.in_progress = True
            tree_node.task_idx = idx
            tree_node.pid = pid
            tree_node.start_time = ts
            attach_task_plan(state, pid, tree_node)
            mark_active(tree_node, 1)
    elif action == "already stored":
        state.active.pop(pid, None)
        covered = leaves_covered(int(m.group("n2")))
        state.pieces_done += covered
        state.joins_done += covered - 1
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
            state.join_events.append(dur)
            state.join_events_by_depth[depth].append(dur)
        if tree_node is not None:
            mark_node_done(tree_node)


def handle_piece(state, content):
    m = RE_PIECE.match(content)
    if not m or m.group("action").strip() != "piece":
        return
    dur = float(m.group("dur"))
    state.pieces_done += 1
    state.piece_events.append(dur)

    entry = state.active.get(int(m.group("pid")))
    depth = entry["depth"] if entry else None
    if depth is not None:
        state.piece_events_by_depth[depth].append(dur)

    if state.tree_by_key:
        i0 = int(m.group("i0"))
        tree_node = state.tree_by_key.get((i0, depth)) if depth is not None else None
        if tree_node is not None:
            mark_leaves_done(tree_node, 1)
            mark_node_done(tree_node)


def handle_phase_line(state, content):
    """split_piece, split_span_res_join and split_big_res_join log phase lines
    (evaluating/loading/multiplying/.../written/locking/locked) and, for joins
    only, a "mul P1xP2" header, all in the same [idx][pid] | i0 n2 depth shape
    as node_process, so RE_NODE_PROCESS parses them too. Track the latest term/micro-phase on the
    matching tree node so they can be shown beside it while it's in progress.
    "locking"/"locked" additionally maintain the set of pids currently
    blocked waiting on the exclusive disk lock, and "locked" counts the
    request and, on MISS, the contention (all tracked here, ahead of the tree
    lookup, so
    they aren't dropped on runs whose tree is too large to display - see
    TREE_NODE_CAP). These lines still fire even when the run's disk lock is
    compiled out (LOCK_DISK_IO undefined, see lib/big/code.c) - disk_lock()
    just becomes a near-instant no-op then, so the counters below keep
    accumulating tiny numbers; the display layer is what hides them once
    state.disk_lock_enabled reads False."""
    m = RE_NODE_PROCESS.match(content)
    if not m:
        return
    action = re.sub(r"\s+", " ", m.group("action").strip())
    pid = int(m.group("pid"))

    entry = state.active.get(pid)
    if entry is not None:
        entry["micro"] = action

    if action == "locking":
        state.locking_pids.add(pid)
    elif action == "locked":
        state.locking_pids.discard(pid)
        state.lock_requests += 1
        token = m.group("lock")
        if token is not None:
            state.lock_tokens_seen = True
            if token == "MISS":
                state.lock_misses += 1

    if not state.tree_by_key:
        return
    i0 = int(m.group("i0"))
    depth = int(m.group("depth"))
    tree_node = state.tree_by_key.get((i0, depth))
    if tree_node is None:
        return
    term = action.removeprefix("mul ")
    if term != action:
        tree_node.term = term
    else:
        tree_node.micro = action


def handle_task_start(state, content):
    m = RE_TASK_START.match(content)
    if not m:
        return
    pid = int(m.group("pid"))
    entry = state.active.setdefault(pid, {"start": float(m.group("ts")), "depth": None, "i0": None})

    mem = m.group("mem")
    if mem is not None:
        entry["mem"] = int(mem)

    thr = m.group("thr")
    if thr is not None:
        if not state.threads_seen:
            state.threads_seen = True
            state.thread_last_ts = float(m.group("ts"))
        entry["threads"] = int(thr)

    if (thr is not None or mem is not None) and state.tree_by_key and entry.get("i0") is not None:
        attach_task_plan(state, pid, state.tree_by_key.get((entry["i0"], entry["depth"])))


def handle_task_end(state, content):
    m = RE_TASK_END.match(content)
    if not m:
        return
    pid = int(m.group("pid"))
    state.active.pop(pid, None)
    state.locking_pids.discard(pid)


def handle_scheduler(state, content):
    m = RE_SCHEDULER.match(content)
    if not m:
        return
    action = m.group("action").strip()
    if action == "active processes":
        state.active_max = max(state.active_max, int(m.group("val")))
    elif action == "active threads":
        state.threads_reported = int(m.group("val"))


def handle_phase(state, content):
    m = RE_CONFIG.match(content)
    if m:
        name, val = m.group("name"), int(m.group("val"))
        if name == "piece size":
            apply_piece_size(val)
            if state.total_pieces is None and state.explicit_size is not None:
                apply_index_max(state, get_index_max(state.explicit_size))
        elif name == "run size":
            # The log wins over --size, which only exists to fill the totals in
            # one line earlier: a --size left over from an earlier run would
            # otherwise size the tree, the progress bar and every ETA for the
            # whole run. Rebuilds only on a real disagreement, so a matching
            # --size keeps the tree it already built.
            if state.total_pieces != val // PIECES_PER_LEAF:
                apply_index_max(state, val)
        elif name == "n process":
            state.n_process_logged = val
        elif name == "mem launch":
            state.mem_launch = val
        elif name == "mem max":
            state.mem_max = val
        elif name == "disk lock":
            state.disk_lock_enabled = bool(val)
        return

    m = RE_PHASE.match(content)
    if not m:
        return
    action = m.group("action").strip()
    ts = m.group("ts")
    if action in ("dividing", "divided", "pi already stored", "binary split solved"):
        set_phase(state, action, ts)
    if action == "pi already stored":
        if state.total_pieces:
            state.pieces_done = state.total_pieces
            state.joins_done = state.total_joins
        if state.tree_root is not None:
            mark_leaves_done(state.tree_root, state.tree_root.leaves_total)
            mark_node_done(state.tree_root, was_active=False)
    elif action == "display begin":
        set_phase(state, "displaying", ts)
    elif action == "display end":
        set_phase(state, "displayed", ts)
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
    "split_piece": handle_phase_line,
    "task_start": handle_task_start,
    "task_end": handle_task_end,
    "scheduler": handle_scheduler,
    "pi_tree": handle_phase,
    "pi_finish": handle_phase,
    "pi_big": handle_phase,
    "pi": handle_phase,
    "split_span": handle_untracked,
    "split_big": handle_untracked,
    "split_span_res_join": handle_phase_line,
    "split_big_res_join": handle_phase_line,
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
    m = RE_TS.match(content)
    if m:
        thread_tick(state, float(m.group("ts")))
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
        return sum(events) / len(events)
    return fallback


def blocked_threads(state):
    """Threads a worker holds but isn't computing on, split by why. A worker
    waiting on the disk lock has all of them idle; one in a read or write has
    all but the thread in the syscall. Anything else counts as working.

    Read off each worker's latest phase line, kept per pid so it survives a run
    whose tree is too large to display (see TREE_NODE_CAP)."""
    waiting = 0
    io = 0
    for entry in state.active.values():
        threads = entry.get("threads")
        micro = entry.get("micro")
        if not threads or micro is None:
            continue
        if micro == "locking":
            waiting += threads
        elif micro.startswith("loading") or micro == "writing":
            io += threads - 1
    return waiting, io


def working_threads(state):
    """Booked threads minus the ones parked on the lock or on I/O, capped at
    n_process: the scheduler can book past its own ceiling (a node overshooting
    mem_max is granted the full width in the first slot, on top of what the
    running tasks already hold), and those extra threads contend for the same
    cores rather than adding any. Uncapped, an oversubscribed stretch would
    bias the utilization average up for the rest of the run."""
    booked = active_threads(state)
    if booked is None:
        return None
    waiting, io = blocked_threads(state)
    runnable = max(0, booked - waiting - io)
    budget = thread_budget(state)
    return min(runnable, budget) if budget else runnable


def thread_util(state, now):
    """Time-weighted average of threads actually computing over the run so far
    - booked threads less those parked on the lock or on I/O. The stretch
    since the last logged line is extrapolated from wall clock at the current
    thread total - without it a single long join, which logs nothing for
    minutes, would leave the average frozen at whatever preceded it."""
    acc, span = state.thread_time, state.thread_span
    if state.last_line_time is not None and not state.done:
        tail = max(0.0, now - state.last_line_time)
        acc += (working_threads(state) or 0) * tail
        span += tail
    if span <= 0:
        return 0.0
    return acc / span


def active_mem_estimate(node):
    if node is None or node.own_done:
        return 0
    total = node.mem_estimate if (node.in_progress and node.mem_estimate is not None) else 0
    for child in node.children:
        total += active_mem_estimate(child)
    return total


# Everything render_tree needs that is the same for every node in the walk.
TreeView = collections.namedtuple(
    "TreeView",
    "lines now avg_piece_dur avg_join_dur piece_events_by_depth join_events_by_depth"
    " pid_rss disk_lock_enabled starved",
)


def render_tree(node, view, prefix="", is_last=True, is_root=True):
    (lines, now, avg_piece_dur, avg_join_dur,
     piece_events_by_depth, join_events_by_depth, pid_rss, disk_lock_enabled, starved) = view
    # `state` drives the recursion cutoff below; `tint` is separate because a
    # node can be partly chewed through with nothing running in it, which must
    # stay expanded but reads as work still ahead.
    #
    # active_count is tested before leaves_done so that every ancestor of a
    # running node shows a lit "\u25cb": that chain is the trail from the root down
    # to the work, and any ancestor with a finished leaf would otherwise break it.
    if node.own_done:
        mark, state, status, tint = "\u2713", "done", None, NODE_DONE
    elif node.in_progress:
        mark, state, status, tint = "\u25b8", "in_progress", str(node.task_idx), NODE_RUNNING
    elif node.children and all(c.own_done for c in node.children):
        # Ready either way; the tint says whether it could actually start.
        mark, state, status = "\u00b7", "ready", None
        tint = NODE_STARVED if starved else NODE_READY
    elif node.active_count > 0:
        mark, state, status, tint = "\u25cb", "in_progress", None, NODE_ACTIVE
    elif node.leaves_done > 0:
        mark, state, status, tint = "\u00b7", "in_progress", None, NODE_PENDING
    else:
        mark, state, status, tint = "\u00b7", "pending", None, NODE_PENDING

    connector = "" if is_root else ("└─ " if is_last else "├─ ")
    if node.kind == "BIG":
        label = f"[{node.depth}, B, {node.i0:,}]"
    else:
        label = f"[{node.depth}, {node.n2}, {node.i0:,}]"
    label = f"{tint}{label}{NODE_OFF}"
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
                    # Signed against the depth's average: unsigned is time
                    # left, "+" is time overrun, and the overrun keeps growing
                    # on screen. A phrase like "any moment" reads calmest
                    # exactly when a task is worst overdue.
                    eta_str = (
                        fmt_duration(remaining) if remaining > 0
                        else "+" + fmt_duration(-remaining)
                    )
                    label += f" ({eta_str})"
                current = pid_rss.get(node.pid) if node.pid is not None else None
                if node.mem_estimate is not None or current is not None:
                    est_str = fmt_bytes(node.mem_estimate) if node.mem_estimate is not None else "?"
                    cur_str = fmt_bytes(current) if current is not None else "?"
                    # Estimate first, measured second - the order is what says
                    # which is which, so it never varies.
                    label += f" | {est_str} / {RSS_ON}{cur_str}{RSS_OFF}"
                # Single-threaded tasks get no badge at all: its presence is
                # what flags a task holding more than one thread slot. "x" is
                # already the multiply in the term below, so the badge takes
                # the multiplication sign to keep the two apart.
                if node.threads is not None and node.threads > 1:
                    label += f" | {MULTI_THR_ON}\u00d7{node.threads}{MULTI_THR_OFF}"
                if node.term:
                    op1, _, op2 = node.term.partition("x")
                    label += f" | {op1} x {op2}"
                if node.micro:
                    micro = node.micro
                    if micro == "locking" and disk_lock_enabled:
                        micro = f"{LOCK_ON}{micro}{LOCK_OFF}"
                    elif micro in ("multiplying", "evaluating"):
                        micro = f"{MUL_ON}{micro}{MUL_OFF}"
                    elif micro.startswith("loading") or micro == "writing":
                        micro = f"{IO_ATTN_ON}{micro}{IO_ATTN_OFF}"
                    label += f" | {micro}"
    lines.append(f"{prefix}{connector}{tint}{mark}{NODE_OFF} {label}")

    if state in ("done", "pending") or (node.children and all(c.own_done for c in node.children)):
        return

    child_prefix = prefix if is_root else prefix + ("   " if is_last else "│  ")
    for i, child in enumerate(node.children):
        render_tree(child, view, child_prefix, i == len(node.children) - 1, is_root=False)


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


# The two steps after the split tree: one big division, then the decimal
# display. Each is a single long step with no sub-progress to count, so each
# counts as one unit against the total.
POST_SPLIT_UNITS = 2


def post_split_done(state):
    if state.done:
        return POST_SPLIT_UNITS
    if state.phase in ("divided", "displaying"):
        return 1
    return 0


BOX_MIN_WIDTH = 24


def render_bar(width, counts, glyphs, mark=None, colour=None, cursor=None, ticks=()):
    """A bracketed bar of `width` cells split between `glyphs` in proportion to
    `counts`. Apportioned by largest remainder, so the segments always sum to
    the full width and a segment is empty only when its count really is zero --
    flooring each independently leaves a stray cell of the last glyph.

    `mark` is a ceiling in the same units as `counts`: a "|" is inserted there,
    widening the bar by one cell and always clamped strictly inside. Used to
    show where n_process falls once the scheduler has booked past it, so an
    oversubscribed bar still shows every booked thread instead of rescaling the
    overshoot away.

    `cursor` is the glyph to draw at the boundary of the first segment, so it
    travels from the left bracket to the right one as that segment fills.
    Its presence is what says "not finished": it stays at the far right while a
    single unit remains and disappears only at completion, so the minimum-one-
    cell rule below is skipped when it is drawn.

    `colour` is one tint for the whole bar, or a tuple of tints parallel to
    `counts` when the segments mean different things and each needs its own.

    `ticks` are (value, glyph, tint) triples in the same units as `counts`,
    each laid on the bar at its own position and each widening it by one cell.
    Unlike `mark` they may sit on either bracket, since a threshold the reading
    has not reached and one it cannot exceed are both worth seeing."""
    tints = colour if isinstance(colour, tuple) else (BAR_ON if colour is None else colour,) * len(counts)
    total = sum(counts)
    if total <= 0:
        return "[" + glyphs[-1] * width + "]"
    raw = [width * c / total for c in counts]
    fills = [int(r) for r in raw]
    order = sorted(range(len(counts)), key=lambda i: raw[i] - fills[i], reverse=True)
    for i in order[:width - sum(fills)]:
        fills[i] += 1
    # Every non-zero count keeps a cell, taken from the largest segment: a
    # progress bar must not read full while work remains, and one busy thread
    # among many must not round away to nothing.
    if not cursor:
        for i, c in enumerate(counts):
            if c > 0 and fills[i] == 0:
                donor = max(range(len(fills)), key=lambda k: fills[k])
                if fills[donor] > 1:
                    fills[donor] -= 1
                    fills[i] += 1
    # One (tint, glyph) per cell, so the marker can be spliced in by cell index
    # without having to reason about where the escapes fall in the string.
    cells = []
    for t, g, n in zip(tints, glyphs, fills):
        cells.extend([(t, g)] * n)
    at, tick = None, "|"
    if cursor:
        if sum(counts[1:]) > 0:
            at, tick = fills[0], cursor
    elif mark is not None and 0 < mark < total:
        # Clamped inside the bar: a marker on either bracket would read as the
        # ceiling being unreached or unexceeded, which is the opposite of why
        # it is drawn.
        at = min(max(round(width * mark / total), 1), width - 1)
    inserts = []
    if at is not None:
        # Left in the default foreground so it reads against the bar rather
        # than as part of it.
        inserts.append((at, (BAR_OFF, tick)))
    for value, glyph, pen in ticks:
        inserts.append((min(max(round(width * value / total), 0), width), (pen, glyph)))
    # Rightmost first, so an insertion never shifts one still to be placed.
    for index, cell in sorted(inserts, key=lambda p: p[0], reverse=True):
        cells.insert(index, cell)
    out, current = [], None
    for t, g in cells:
        if t != current:
            out.append(t)
            current = t
        out.append(g)
    # Only the segments are tinted; the brackets frame the bar and stay default.
    return "[" + "".join(out) + BAR_OFF + "]"


def cursor_glyph(state):
    """Glyph for the progress cursor, advanced one step per frame that brought
    new log lines. A frame with none still advances a stub to full height, so
    the mark always settles at rest within one frame instead of freezing
    half-drawn -- a still mark reads as paused, half a mark reads as a glitch.
    The stall warning still covers a real hang; this only fills the minute
    before it fires, so a fresh run can be seen breathing."""
    moved = state.cursor_lines != state.lines_seen
    state.cursor_lines = state.lines_seen
    if state.done or (not moved and CURSOR_PULSE[state.cursor_phase] == CURSOR_REST):
        return CURSOR_REST
    state.cursor_phase = (state.cursor_phase + 1) % len(CURSOR_PULSE)
    return CURSOR_PULSE[state.cursor_phase]


def render_box(title, rows, width):
    """A rectangle with its title set into the top edge. Rows are padded or
    clipped to the inner width, so every line is exactly `width` columns."""
    inner = max(BOX_MIN_WIDTH, width - 2)
    head = f"\u250c\u2500 {title} " if title else "\u250c"
    head += "\u2500" * max(0, inner + 2 - len(head) - 1) + "\u2510"
    out = [head]
    for row in rows:
        out.append("\u2502 " + _fit_visible(row, inner - 1) + "\u2502")
    out.append("\u2514" + "\u2500" * inner + "\u2518")
    return out


def render_row(boxes, gap):
    """Lay rendered boxes side by side, padding the shorter to the taller."""
    widths = [len(ANSI_SGR_RE.sub("", b[0])) for b in boxes]
    height = max(len(b) for b in boxes)
    return [
        (" " * gap).join(b[i] if i < len(b) else " " * w for b, w in zip(boxes, widths))
        for i in range(height)
    ]


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
    lines.append(f"elapsed: {fmt_duration(elapsed)} (since dashboard attached)")
    if state.run_start_time is not None:
        run_elapsed = now - state.run_start_time
        lines.append(f"run:     {fmt_duration(run_elapsed)} (since process start)")

    piece_durs = list(state.piece_events)
    avg_piece_dur = sum(piece_durs) / len(piece_durs) if piece_durs else None
    join_durs = list(state.join_events)
    avg_join_dur = sum(join_durs) / len(join_durs) if join_durs else None

    if state.done:
        lines.append("")
        lines.append("*** PROCESS DONE ***")

    if state.last_line_time is not None and not state.done:
        since_last_line = now - state.last_line_time
        # Both kinds of work, not just pieces: a run whose pieces all came from
        # cache has no piece durations at all, and a single long join logs
        # nothing for far longer than the 60s floor.
        work_durs = piece_durs + join_durs
        stall_threshold = max(60.0, 2 * max(work_durs)) if work_durs else 60.0
        if since_last_line > stall_threshold:
            lines.append(
                f"{ALERT_ON}WARNING: no log activity for {fmt_duration(since_last_line)} "
                f"- the run may have stopped{ALERT_OFF}"
            )

    lines.append("")

    ram_used, ram_total = get_system_ram()
    pid_rss = get_pid_rss(state.active.keys())

    # Geometry first: completion and threads share a row, ram spans beneath, so
    # each bar has to be sized to the column it lands in.
    BOX_INSET = 2
    BOX_GAP = 2
    avail = shutil.get_terminal_size(fallback=(80, 24)).columns - 2 * BOX_INSET
    left_w = (avail - BOX_GAP) // 2
    right_w = avail - BOX_GAP - left_w
    # Side by side only while a half can still hold the widest row either box
    # has; below that they stack full width rather than clipping their own text.
    TWO_COL_MIN = 50
    two_col = left_w >= TWO_COL_MIN
    if not two_col:
        left_w = right_w = avail
    # Every box shares one label column, and every bar is indented to it.
    LABEL_W = 9
    # inner width, less "\u2502 ", the indent, the brackets, and a trailing
    # column so a full bar doesn't butt against the right border
    done_bar_w = max(10, left_w - 15)
    thread_bar_w = max(10, right_w - 15)
    ram_bar_w = max(10, left_w - 15)
    disk_bar_w = max(10, right_w - 15)

    # --- box 1: overall completion ---
    completion_lines = []
    phase_line = "phase:".ljust(LABEL_W) + state.phase
    if not state.done and state.phase != "splitting" and state.phase_start_time:
        phase_line += f"   {fmt_duration(time.time() - state.phase_start_time)} elapsed"
    completion_lines.append(phase_line)
    if state.total_pieces:
        total_joins = state.total_joins
        split_units = state.total_pieces + total_joins
        total_units = split_units + POST_SPLIT_UNITS
        done_units = min(state.pieces_done + state.joins_done, split_units) + post_split_done(state)
        pct = 100.0 * done_units / total_units
        # The cursor adds a cell, so give the segments one less and the bar keeps
        # the width it has once the run finishes and the cursor goes away.
        remaining = total_units - done_units
        bar = render_bar(
            done_bar_w - 1 if remaining else done_bar_w,
            (done_units, remaining),
            BAR_FULL + BAR_NONE, cursor=cursor_glyph(state),
        )
        # The split tree's own counts only mean anything while it is being
        # walked; past that the phase line carries the progress.
        if state.phase == "splitting":
            completion_lines.append(
                "pieces:".ljust(LABEL_W) + f"{state.pieces_done} / {state.total_pieces}    joins: {state.joins_done} / {total_joins}"
            )
        completion_lines.append("overall:".ljust(LABEL_W) + f"{done_units} / {total_units}  ({pct:5.1f}%)")
        completion_lines.append(" " * LABEL_W + bar)
    else:
        completion_lines.append(
            "pieces:".ljust(LABEL_W) + f"{state.pieces_done} / ?    joins: {state.joins_done} / ? (waiting for the log's \"piece size\"/\"run size\" lines)"
        )

    # --- box 2: thread occupancy ---
    thread_lines = []
    n_workers = state.n_process_logged or state.n_process or state.active_max
    # active_max counts processes, so it is no stand-in for the thread budget:
    # one 16-thread task runs as a single process.
    budget = thread_budget(state)
    threads_now = active_threads(state)
    waiting, io = blocked_threads(state)
    blocked_total = waiting + io

    # Same shape as the completion box: a label column, values aligned under it,
    # and the bar indented to the value column instead of flush to the border.
    PAIR_COL = 25

    def pair(left, right):
        """Two halves of a row, the right one starting at PAIR_COL. ljust is a
        no-op once the left half has run past the column, so pad by hand rather
        than let the halves run together."""
        if not right:
            return left
        return (left.ljust(PAIR_COL) if len(left) < PAIR_COL else left + "  ") + right

    # Both rows carry the same "blocked: n" half, and neither carries it while
    # nothing is blocked: a standing "0" is noise on a healthy run.
    workers_blocked = len(state.locking_pids)
    thread_lines.append(pair(
        f"workers: {len(state.active)} / {n_workers or '?'}",
        f"blocked: {workers_blocked}" if workers_blocked else "",
    ))

    if threads_now is not None:
        threads_row = f"threads: {threads_now} / {budget or '?'}"
        if budget and threads_now > budget:
            threads_row += f" (+{threads_now - budget} over)"
        why = ", ".join(
            part for part in (
                f"{waiting} lock" if waiting else "",
                f"{io} I/O" if io else "",
            ) if part
        )
        blocked_half = ""
        if blocked_total:
            blocked_half = f"blocked: {blocked_total}" + (f"  ({why})" if why else "")
        thread_lines.append(pair(threads_row, blocked_half))

    if budget and threads_now is not None:
        # Booked and running, booked but parked on the lock or on I/O, and
        # unbooked. The counts sum to max(threads_now, budget), so every booked
        # thread keeps a cell when the scheduler has overbooked, and the marker
        # says where n_process falls among them.
        parked = min(blocked_total, threads_now)
        counts = (threads_now - parked, parked, max(0, budget - threads_now))
        mark = budget if threads_now > budget else None
        # The marker adds a cell, so give the segments one less and the bar
        # keeps the width it has when nothing is overbooked.
        bar_w = thread_bar_w - 1 if mark else thread_bar_w
        # Inverted severity: a full bar is the good end, so the ramp reads
        # 1 - working/budget. Alarm at 0.5 puts half the threads idle already
        # at the hottest tone - on a run this size, half the machine doing
        # nothing is not a shade of concern, it is the failure.
        working_frac = counts[0] / budget
        # Working and parked share the tone; only the glyph tells them apart.
        # The colour answers one question - how much of the budget is doing
        # work - and parked threads are already counted against it, so giving
        # them a second colour would state the same fact twice.
        tint = severity_colour(1.0 - working_frac, alarm=0.5)
        tints = (tint, tint, BAR_ON)
        thread_lines.append(" " * LABEL_W + render_bar(
            bar_w, counts, BAR_FULL + BAR_HELD + BAR_NONE, mark, colour=tints,
        ))

    if state.thread_span > 0:
        util = thread_util(state, now)
        pct = f" ({100.0 * util / budget:.0f}%)" if budget else ""
        thread_lines.append(" " * LABEL_W + f"util: {util:.1f} / {budget or '?'} avg{pct}")


    # --- box 3: ram usage ---
    ram_lines = []
    over_launch = False
    est = real = None
    if ram_total is not None:
        row = "ram:".ljust(LABEL_W) + f"{fmt_bytes(ram_used)} / {fmt_bytes(ram_total)}"
        if ram_used is not None:
            row += f" ({100.0 * ram_used / ram_total:4.1f}%)"
        ram_lines.append(row)
    if pid_rss or state.tree_root is not None:
        est = active_mem_estimate(state.tree_root) if state.tree_root is not None else None
        real = sum(pid_rss.values()) if pid_rss else None
        est_str = fmt_bytes(est) if est is not None else "?"
        real_str = fmt_bytes(real) if real is not None else "?"
        # "est / launch..max": below launch new tasks start, above it the
        # scheduler holds back until a task ends; max is never crossed.
        if state.mem_launch and state.mem_max:
            limit_str = f" / {fmt_bytes(state.mem_launch)}..{fmt_bytes(state.mem_max)}"
        elif state.mem_launch or state.mem_max:
            limit_str = f" / {fmt_bytes(state.mem_launch or state.mem_max)}"
        else:
            limit_str = ""
        # "held": usage has reached mem_launch, so nothing new launches until a
        # task ends. Worth flagging - it is the state a stalling run sits in.
        over_launch = est is not None and state.mem_launch and est >= state.mem_launch
        held = f" {ALERT_ON}held{ALERT_OFF}" if over_launch else ""
        ram_lines.append("workers:".ljust(LABEL_W) + f"{est_str}{limit_str}{held} | {real_str}")
    mem_budget = state.mem_max or state.mem_launch
    if est is not None and mem_budget:
        # The bar ends at the budget and stretches only far enough to keep an
        # overshoot on screen, snapping back once it is gone: the limit marks
        # then sit still for as long as nothing is wrong, and moving marks are
        # themselves the signal that something is.
        scale = max(mem_budget, est, real or 0)
        # A dotted rule at each limit: launch in the default foreground, max in
        # coral, since one holds launches back and the other is the ceiling.
        ticks = []
        if state.mem_launch and state.mem_launch != state.mem_max:
            ticks.append((state.mem_launch, BAR_LIMIT, BAR_OFF))
        if state.mem_max:
            ticks.append((state.mem_max, BAR_LIMIT, BAR_ALERT))
        # Measured RSS alongside the estimate the scheduler actually gates on:
        # the gap between them is the estimator being wrong, which is worth
        # seeing at the moment it happens rather than afterwards.
        if real is not None:
            ticks.append((real, BAR_MEASURED, BAR_OFF))
        ram_lines.append(" " * LABEL_W + render_bar(
            ram_bar_w - len(ticks), (est, scale - est), BAR_FULL + BAR_NONE,
            colour=severity_colour(est / mem_budget), ticks=tuple(ticks),
        ))
    elif ram_total is not None and ram_used is not None:
        # No budget in the log yet, so fall back to the machine's own usage.
        # Memory is comfortable well past half the machine; it only starts to
        # matter once the page cache has nowhere left to go. See RAM_CALM/ALARM.
        used = min(ram_used, ram_total)
        ram_lines.append(" " * LABEL_W + render_bar(
            ram_bar_w, (used, ram_total - used), BAR_FULL + BAR_NONE,
            colour=severity_colour(used / ram_total, RAM_CALM, RAM_ALARM),
        ))

    # --- box 4: disk ---
    disk_lines = []
    numbers_size = get_dir_size(os.path.join(CACHE_DIR, "numbers"))
    pieces_size = get_dir_size(os.path.join(CACHE_DIR, "pieces"))
    disk_lines.append(
        "cache:".ljust(LABEL_W) + f"numbers {fmt_bytes(numbers_size)}   pieces {fmt_bytes(pieces_size)}"
    )
    if state.lock_requests and state.disk_lock_enabled is not False:
        if state.lock_tokens_seen:
            pct = 100.0 * state.lock_misses / state.lock_requests
            misses = f"{state.lock_misses} misses ({pct:.0f}%)"
        else:
            # No "locked" line carried HIT/MISS: a log from a build predating
            # them. Zero misses would read as no contention rather than no data.
            misses = "misses n/a (stale build)"
        disk_lines.append("lock:".ljust(LABEL_W) + f"{state.lock_requests} requests, {misses}")
        if state.lock_tokens_seen:
            # The miss ratio is the worry, so the bar is tinted by its own value
            # rather than by the slate every other bar uses.
            frac = state.lock_misses / state.lock_requests
            disk_lines.append(" " * LABEL_W + render_bar(
                disk_bar_w,
                (state.lock_misses, state.lock_requests - state.lock_misses),
                BAR_FULL + BAR_NONE,
                colour=severity_colour(frac),
            ))

    inset = " " * BOX_INSET
    grid = (
        (("completion", completion_lines), ("threads", thread_lines)),
        (("ram", ram_lines), ("disk", disk_lines)),
    )
    for pair in grid:
        present = [(t, r) for t, r in pair if r]
        if not present:
            continue
        if two_col and len(present) == 2:
            # Pad the shorter of a pair so both frames close on the same row.
            height = max(len(rows) for _, rows in present)
            present = [(t, rows + [""] * (height - len(rows))) for t, rows in present]
            boxes = [render_box(t, r, w) for (t, r), w in zip(present, (left_w, right_w))]
            lines.extend(inset + row for row in render_row(boxes, BOX_GAP))
        else:
            for title, rows in present:
                lines.extend(inset + row for row in render_box(title, rows, avail))

    # Only while the tree is being walked: past that every node is done and the
    # view says nothing the completion box doesn't.
    if state.phase == "splitting":
        lines.append("")
        # A ready node needs both a free thread and room under mem_launch to
        # start. With neither, the whole ready set is queued rather than about
        # to run, and the tree says so by dimming it.
        starved = bool(over_launch or (budget and threads_now is not None and threads_now >= budget))
        if state.tree_root is not None:
            render_tree(state.tree_root, TreeView(
                lines, now, avg_piece_dur, avg_join_dur,
                state.piece_events_by_depth, state.join_events_by_depth, pid_rss,
                state.disk_lock_enabled is not False, starved,
            ))
        elif state.tree_skipped_reason:
            lines.append(f"tree: {state.tree_skipped_reason}")

    return "\n".join(lines)


RESTARTED = object()


def tail(path):
    """Yield whole log records as they land, RESTARTED when the file is replaced.

    tprintf() writes a record as "\\n" + payload + "\\t", so the newline that
    separates two records belongs to the later one and the newest record in the
    file carries no terminator of its own - which is why an unterminated read
    cannot simply be held back, or a finished run's "display end" would never
    arrive. A record is complete once it ends in the tab; short of that the
    writer (tee, whose reads split a busy pipe mid-record) is caught halfway,
    and the fragment is held until the rest lands. Feeding the halves through
    separately would lose the record entirely - neither half parses, and a
    dropped piece or join never comes back.
    """
    inode = None
    f = None
    pending = ""
    try:
        while True:
            try:
                st = os.stat(path)
            except OSError:
                if f is not None:
                    f.close()
                    f = None
                    inode = None
                    pending = ""
                    yield RESTARTED
                yield None
                time.sleep(0.3)
                continue

            if f is None or st.st_ino != inode:
                if f is not None:
                    f.close()
                f = open(path, "r", errors="replace")
                inode = st.st_ino
                pending = ""
                yield RESTARTED

            chunk = f.readline()
            if not chunk:
                yield None
                time.sleep(0.2)
                continue

            pending += chunk
            if pending.endswith("\n") or pending.endswith("\t"):
                line, pending = pending, ""
                yield line
    finally:
        if f is not None:
            f.close()


def _fit_visible(line, width):
    """Truncate/pad line to width visible columns, treating ANSI SGR escapes
    (zero-width) as free so they're never split mid-sequence - a naive
    line[:width] slice could otherwise cut an escape in half and leak color
    into the rest of the screen."""
    out = []
    visible = 0
    i, n = 0, len(line)
    while i < n:
        m = ANSI_SGR_RE.match(line, i)
        if m:
            out.append(m.group())
            i = m.end()
            continue
        if visible < width:
            out.append(line[i])
            visible += 1
        i += 1
    if visible < width:
        out.append(" " * (width - visible))
    return "".join(out)


def read_pending_input(fd):
    """Drain whatever's currently buffered on fd without blocking."""
    chunks = []
    while select.select([fd], [], [], 0)[0]:
        chunk = os.read(fd, 1024)
        if not chunk:
            break
        chunks.append(chunk)
    return b"".join(chunks)


def parse_scroll_actions(raw):
    """Turn raw stdin bytes into a list of scroll actions: ("delta", +-1),
    ("page", +-1), ("top",), ("bottom",) or ("quit",). Arrow/PgUp/PgDn/Home/End
    keys arrive as multi-byte "\\x1b[..." escape sequences; j/k/g/G/q are
    plain single bytes."""
    actions = []
    i, n = 0, len(raw)
    while i < n:
        b = raw[i]
        if b == 0x1B:
            if i + 2 < n and raw[i + 1] == 0x5B:
                c = raw[i + 2]
                if c == 0x41:  # up
                    actions.append(("delta", -1))
                    i += 3
                    continue
                if c == 0x42:  # down
                    actions.append(("delta", 1))
                    i += 3
                    continue
                if c == 0x35 and i + 3 < n and raw[i + 3] == 0x7E:  # PgUp
                    actions.append(("page", -1))
                    i += 4
                    continue
                if c == 0x36 and i + 3 < n and raw[i + 3] == 0x7E:  # PgDn
                    actions.append(("page", 1))
                    i += 4
                    continue
                if c == 0x48:  # Home
                    actions.append(("top",))
                    i += 3
                    continue
                if c == 0x46:  # End
                    actions.append(("bottom",))
                    i += 3
                    continue
            i += 1
            continue
        ch = chr(b)
        if ch == "j":
            actions.append(("delta", 1))
        elif ch == "k":
            actions.append(("delta", -1))
        elif ch == "g":
            actions.append(("top",))
        elif ch == "G":
            actions.append(("bottom",))
        elif ch == "q" or b == 0x03:  # q or Ctrl-C (ISIG is off in cbreak+no-echo mode)
            actions.append(("quit",))
        i += 1
    return actions


def draw(state, scroll_offset=0, actions=()):
    """Render state, scrolled by scroll_offset lines and adjusted by actions
    (see parse_scroll_actions), reserving the last row as a position/help
    footer whenever the content taller than the terminal. Returns the
    clamped scroll_offset so the caller can carry it into the next frame."""
    cols, rows = shutil.get_terminal_size(fallback=(80, 24))
    all_lines = render(state).split("\n")
    body_rows = max(1, rows - 1)
    max_offset = max(0, len(all_lines) - body_rows)

    for action in actions:
        kind = action[0]
        if kind == "delta":
            scroll_offset += action[1]
        elif kind == "page":
            scroll_offset += action[1] * max(1, body_rows - 2)
        elif kind == "top":
            scroll_offset = 0
        elif kind == "bottom":
            scroll_offset = max_offset
    scroll_offset = max(0, min(scroll_offset, max_offset))

    visible = all_lines[scroll_offset:scroll_offset + body_rows]
    content = [_fit_visible(line, cols) for line in visible]
    content += [" " * cols] * (body_rows - len(content))

    if max_offset > 0:
        footer = (
            f" lines {scroll_offset + 1}-{min(scroll_offset + body_rows, len(all_lines))}/{len(all_lines)}"
            "   ↑/↓ j/k scroll   PgUp/PgDn page   g/G top/bottom   q quit "
        )
    else:
        footer = " q quit "
    content.append(_fit_visible(footer, cols))

    # Every line is already padded to exactly `cols` and there are always
    # `rows` lines, so a full repaint doesn't need a clear - clearing then
    # redrawing every frame is what caused the flicker (a blank frame flashes
    # between the erase and the repaint). Only clear when the terminal was
    # actually resized, since old content could otherwise show through around
    # a shrunk frame.
    if (cols, rows) != draw.last_dims:
        sys.stdout.write("\x1b[H\x1b[2J")
        draw.last_dims = (cols, rows)
    else:
        sys.stdout.write("\x1b[H")
    sys.stdout.write("\n".join(content))
    sys.stdout.flush()
    return scroll_offset


draw.last_dims = None


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
    scroll_offset = 0

    stdin_fd = sys.stdin.fileno()
    is_tty = sys.stdin.isatty()
    old_termios = None
    if is_tty:
        old_termios = termios.tcgetattr(stdin_fd)
        tty.setcbreak(stdin_fd)
        no_echo = termios.tcgetattr(stdin_fd)
        no_echo[3] &= ~termios.ECHO  # cbreak for immediate reads, but don't echo scroll keys
        termios.tcsetattr(stdin_fd, termios.TCSADRAIN, no_echo)

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

            actions = parse_scroll_actions(read_pending_input(stdin_fd)) if is_tty else []
            if any(action[0] == "quit" for action in actions):
                raise KeyboardInterrupt

            now = time.time()
            if actions or now - last_render >= 1.0:
                if not state.done:
                    state.process_running = pi_process_running()
                    if state.process_running:
                        state.ever_saw_process = True
                        state.process_gone_since = None
                        if state.run_start_time is None:
                            state.run_start_time = get_run_start_time()
                    elif state.process_gone_since is None:
                        state.process_gone_since = now
                scroll_offset = draw(state, scroll_offset, actions)
                last_render = now

            if state.done and state.ever_saw_process and not done_announced:
                scroll_offset = draw(state, scroll_offset)
                done_announced = True
    finally:
        sys.stdout.write("\x1b[0m\x1b[?25h\x1b[?1049l")
        sys.stdout.flush()
        if old_termios is not None:
            termios.tcsetattr(stdin_fd, termios.TCSADRAIN, old_termios)


def _raise_keyboard_interrupt(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, _raise_keyboard_interrupt)
    try:
        main()
    except KeyboardInterrupt:
        pass
