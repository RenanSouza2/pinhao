#!/usr/bin/env python3
"""Seconds per limb of the loads, writes and adds in each run log, by limbs
moved and by quarter of the split, as a curve-fit dashboard.py books them."""

import argparse
import statistics

from common import DASHBOARD, LOGS, load_dashboard, replay

SIZE_EDGES = (0, 1e6, 4e6, 16e6, 64e6, float("inf"))  # limbs moved
PHASES = ("loading", "writing", "adding")


def events(d, log):
    """(phase, limbs, seconds, stamp) of every load, write and add, and the
    split's first and last stamps."""
    out = []
    book = d.book_phase_end

    def booked(state, node, action, ts):
        before = {k: tuple(v) for k, v in state.io_sums.items()}
        book(state, node, action, ts)
        for k, v in state.io_sums.items():
            if tuple(v) != before[k]:
                out.append((k, v[0] - before[k][0], v[1] - before[k][1], ts))

    d.book_phase_end = booked
    state, stamps = replay(d, log)
    return out, state.log_start, stamps.get("binary split solved", state.log_time)


def ns_per_limb(xs):
    return 1e9 * sum(e[2] for e in xs) / max(sum(e[1] for e in xs), 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", default=LOGS, help="run logs (default: the 128M and 256M runs)")
    parser.add_argument("--dashboard", default=DASHBOARD, help="dashboard.py to replay (default: this checkout's)")
    args = parser.parse_args()
    for log in args.logs:
        ev, t0, t1 = events(load_dashboard(args.dashboard, internals=True), log)
        total = sum(e[2] for e in ev)
        print(f"== {log}: {len(ev)} loads/writes/adds, {total / 3600:.1f} h in all")
        print("   limbs moved      " + "".join(f"{p + ' ns/limb':>22s}" for p in PHASES) + "   share of the time")
        for lo, hi in zip(SIZE_EDGES, SIZE_EDGES[1:]):
            cells = []
            for phase in PHASES:
                xs = [e for e in ev if e[0] == phase and lo <= e[1] < hi]
                cells.append(f"{ns_per_limb(xs):8.0f} (n={len(xs):5d})" if xs else f"{'-':>8s}          ")
            share = sum(e[2] for e in ev if lo <= e[1] < hi) / total
            print(f"   {lo / 1e6:5.0f}M..{hi / 1e6:<6.0f}M " + "  ".join(f"{c:>20s}" for c in cells) + f"   {100 * share:5.1f}%")
        print("   quarter of the split     loading ns/limb (median limbs)   writing ns/limb (median limbs)")
        for k in range(4):
            a, b = t0 + (t1 - t0) * k / 4, t0 + (t1 - t0) * (k + 1) / 4
            cells = []
            for phase in ("loading", "writing"):
                xs = [e for e in ev if e[0] == phase and a <= e[3] < b]
                cells.append(f"{ns_per_limb(xs):6.0f} ({statistics.median(e[1] for e in xs) / 1e6:5.1f}M)" if xs else "-")
            print(f"   {k + 1:>20d}  " + "".join(f"{c:>31s}" for c in cells))


if __name__ == "__main__":
    main()
