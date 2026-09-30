#!/usr/bin/env python3
"""Replay run logs through a dashboard.py and score its run ETA against each
run's real end, one reading a log minute. --patch applies an experiment to the
dashboard first.

Per log: median |error| by quarter of the run, share of readings early, error
range past the first 5%, and the 95th-percentile and largest move between two
readings, all in minutes."""

import argparse
import math
import os
import statistics

from common import DASHBOARD, LOGS, load_dashboard, replay


def patch_piece_all(d):
    """Pieces priced at the median of every piece, not the last PIECE_WINDOW."""
    d.PIECE_WINDOW = 10 ** 9


def patch_mul_fit_a(d):
    """The multiplication scale from the log-space fit's intercept, not the
    median over every multiplication."""
    fit_cost = d.fit_cost

    def patched(state):
        cost = fit_cost(state)
        if cost.a is not None:
            n = sx = sy = 0.0
            for threads, (k, kx, _, ky, _, _) in state.mul_fit.items():
                c = math.log(cost.s + (1.0 - cost.s) / threads)
                n += k
                sx += kx
                sy += ky - k * c
            cost.a = math.exp((sy - cost.b * sx) / n)
        return cost

    d.fit_cost = patched


PATCHES = {"piece_all": patch_piece_all, "mul_fit_a": patch_mul_fit_a}


def score(t0, end, readings):
    dur = end - t0
    err = [((t - t0) / dur, (t + left - end) / 60) for t, left in readings]
    quarters = [[abs(e) for p, e in err if k / 4 <= p < (k + 1) / 4] for k in range(4)]
    late = [e for p, e in err if p > 0.05]
    moves = sorted(abs(b[1] - a[1]) for a, b in zip(err, err[1:]))
    return ("/".join(f"{statistics.median(q):5.1f}" if q else "    -" for q in quarters),
            f"early {100 * sum(e < 0 for _, e in err) / len(err):3.0f}%",
            f"range {min(late):+4.0f}..{max(late):+4.0f}",
            f"jump p95 {moves[int(0.95 * len(moves))]:4.1f} max {moves[-1]:4.1f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", default=LOGS, help="run logs (default: the 128M and 256M runs)")
    parser.add_argument("--dashboard", default=DASHBOARD, help="dashboard.py to score (default: this checkout's)")
    parser.add_argument("--patch", action="append", default=[], choices=sorted(PATCHES),
                        help="experiment to apply first; repeatable")
    args = parser.parse_args()
    for log in args.logs:
        d = load_dashboard(args.dashboard)
        for name in args.patch:
            PATCHES[name](d)
        readings = []

        def on_frame(state, now):
            left = d.run_time_left(state, d.fit_cost(state), now)
            if left is not None:
                readings.append((now, left))

        state, stamps = replay(d, log, on_frame)
        print(os.path.basename(log), *score(state.log_start, stamps["display end"], readings), sep="  ")


if __name__ == "__main__":
    main()
