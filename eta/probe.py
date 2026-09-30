#!/usr/bin/env python3
"""Replay one run log through a curve-fit dashboard.py and save as JSON, a log
minute apart, what the ETA's cost fit had seen (multiplications, pieces, the
spread of sizes multiplied), the work it still priced by part - pieces,
multiplications, loads/writes/adds - and the run's thread use and launch
halts. anatomy.py reads it."""

import argparse
import collections
import json

from common import DASHBOARD, MILESTONES, load_dashboard, replay, work_totals


def work_left_parts(d, state, cost, piece_sizes, now):
    """run_work_left, kept apart by piece, multiplication and load/write/add."""
    memo = {}
    parts = collections.Counter()
    stack = [state.tree_root]
    while stack:
        node = stack.pop()
        if node.own_done:
            continue
        stack.extend(node.children)
        if not node.children:
            spent = now - node.start_time if node.start_time is not None else 0.0
            parts["piece"] += max(cost.piece - spent, 0.05 * cost.piece)
            continue
        if node.op_limbs is not None:
            limbs = d.step_limbs(node.op_limbs, d.exact_path(node.op_limbs, node.kind, cost.size), cost.size)
        else:
            lr = d.result_limbs(node.children[0], cost.size, piece_sizes, memo)
            rr = d.result_limbs(node.children[1], cost.size, piece_sizes, memo)
            op = lr[:3] + rr[:3]
            limbs = d.step_limbs(op, node.kind == "SPAN" and lr[3] and rr[3] and op[0] < cost.size, cost.size)
        running = d.step_running(node)
        for k in range(node.steps_done, len(d.JOIN_STEPS)):
            phase = d.JOIN_STEPS[k][1]
            w = cost.work(limbs[k]) if phase == "multiplying" else cost.rates[phase] * limbs[k]
            if running and k == node.steps_done:
                spent = now - node.micro_start
                if phase == "multiplying":
                    spent *= cost.speedup(node.mul_threads)
                w = max(w - spent, 0.05 * w)
            parts["mul" if phase == "multiplying" else "io"] += w
    return parts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    parser.add_argument("out", help="JSON to write")
    parser.add_argument("--dashboard", default=DASHBOARD, help="dashboard.py to replay (default: this checkout's)")
    args = parser.parse_args()
    d = load_dashboard(args.dashboard, internals=True)

    frames = []
    halts = collections.Counter()  # launch halt reason -> times seen

    def on_frame(state, now):
        cost = d.fit_cost(state)
        rest = d.run_time_left(state, cost, now)
        rec = {"t": now, "rest": rest, "phase": state.phase, "n_mul": len(state.mul_samples),
               "n_piece": len(state.piece_events), "joins": state.joins_done, "n_thr": len(state.mul_fit),
               "spread": state.mul_limbs_hi / state.mul_limbs_lo if state.mul_limbs_lo else 0,
               "a": cost.a, "b": cost.b, "s": cost.s, "piece": cost.piece,
               "booked": d.booked_capped(state) or 0, "working": d.working_threads(state) or 0,
               "booked_time": state.booked_time, "thread_time": state.thread_time, "span": state.thread_span,
               "acc": work_totals(state), "halts": dict(halts)}
        if rest is not None and state.phase == "splitting":
            rec["parts"] = work_left_parts(d, state, cost, d.piece_sizes_at(state), now)
            rec["done"] = d.run_work_done(state, cost, now)
            rec["rate"] = (now - state.log_start) / rec["done"]
        frames.append(rec)

    def on_line(state, line):
        func, sep, content = line.partition("|")
        if sep and func.strip() == "node_can_launch":
            m = d.RE_HALT.match(content.strip())
            if m:
                halts[m.group("reason")] += 1

    state, stamps = replay(d, args.log, on_frame, on_line)
    with open(args.out, "w") as f:
        json.dump({"frames": frames, "ms": {k: stamps[k] for k in MILESTONES if k in stamps},
                   "t0": state.log_start, "final": work_totals(state)}, f)
    print(f"{args.log}: {len(frames)} readings -> {args.out}")


if __name__ == "__main__":
    main()
