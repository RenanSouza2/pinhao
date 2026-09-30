"""Paths, the dashboard loader and the replay loop the ETA scripts share."""

import importlib.util
import os
import sys

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DASHBOARD = os.path.join(REPO_ROOT, "dashboard.py")
# the 128M and 256M runs
LOGS = [os.path.join(REPO_ROOT, "thread_log", name)
        for name in ("run.2026-09-29_11-54-14.log", "run.2026-09-29_22-05-35.log")]

# log seconds between two readings
FRAME = 60.0

MILESTONES = ("binary split solved", "dividing", "divided", "display begin", "display end")


def load_dashboard(path=DASHBOARD, internals=False):
    """The dashboard.py at `path` as a fresh module in replay mode. With
    `internals`, one whose ETA is the curve fit's work left over the rate."""
    spec = importlib.util.spec_from_file_location("dashboard_replayed", path)
    d = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(d)
    if internals and not hasattr(d, "run_work_left"):
        sys.exit(f"{path}: no run_work_left; this script reads the curve-fit ETA, pass --dashboard with one that has it")
    d.DISPLAY.replaying = True
    return d


def replay(d, path, on_frame=None, on_line=None):
    """Feed the log at `path` to a fresh State, calling on_frame(state, now)
    every FRAME seconds of log time until the run is done and on_line(state,
    line) after every line. Returns the State and each milestone's stamp."""
    state = d.State()
    stamps, due = {}, None
    with open(path, errors="replace") as f:
        for line in f:
            _, sep, content = line.rstrip("\n").partition("|")
            m = d.RE_STAMP.match(content.strip()) if sep else None
            ts = float(m.group("ts")) if m else None
            if on_frame and ts is not None and state.log_start is not None:
                due = due or state.log_start + FRAME
                while ts >= due and not state.done:
                    on_frame(state, max(due, state.log_time))
                    due += FRAME
            d.feed_line(state, line)
            if m and m.group("action").strip() in MILESTONES:
                stamps[m.group("action").strip()] = ts
            if on_line:
                on_line(state, line)
    return state, stamps


def work_totals(state):
    """Seconds of pieces and of loads, writes and adds, and seconds multiplied
    by thread count, finished so far."""
    return {"piece": state.piece_time, "io": state.io_time,
            "mul": {str(k): v for k, v in state.mul_seconds.items()}}


def speedup(s, threads):
    return 1.0 / (s + (1.0 - s) / max(int(threads), 1))
