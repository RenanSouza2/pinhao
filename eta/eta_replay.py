#!/usr/bin/env python3
"""Replay one run log through a curve-fit dashboard.py and save as JSON, a log
minute apart, its run ETA and what that is made of - work done and left, the
rate, the post-split estimate, the fitted cost - plus every running task's own
ETA and when the task really ended. score.py and diag.py read it."""

import argparse
import collections
import json

from common import DASHBOARD, MILESTONES, load_dashboard, replay, work_totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    parser.add_argument("out", help="JSON to write")
    parser.add_argument("--dashboard", default=DASHBOARD, help="dashboard.py to replay (default: this checkout's)")
    args = parser.parse_args()
    d = load_dashboard(args.dashboard, internals=True)

    frames = []
    node_obs = collections.defaultdict(list)  # id -> [(now, predicted end)]
    node_info, node_end, watched = {}, {}, set()

    def on_frame(state, now):
        cost = d.fit_cost(state)
        rest = d.run_time_left(state, cost, now)
        rec = {"t": now, "rest": rest, "phase": state.phase}
        if rest is not None:
            mul = cost.work(2 * cost.size) / cost.speedup(d.thread_budget(state))
            rec["post"] = d.post_split_left(state, mul, now)
        if rest is not None and state.phase == "splitting":
            done = d.run_work_done(state, cost, now)
            root = state.tree_root
            rec.update(done=done, left=d.run_work_left(state, cost, d.piece_sizes_at(state), now),
                       rate=(now - state.log_start) / done, a=cost.a, b=cost.b, s=cost.s,
                       piece=cost.piece, rates=cost.rates, pieces_done=state.pieces_done,
                       joins_done=state.joins_done, units=root.units_done / root.subtree_weight,
                       acc=work_totals(state))
        frames.append(rec)
        for entry in state.active.values():
            node = entry.get("node")
            left = None if node is None else d.node_time_left(node, cost, now)
            if left is None:
                continue
            node_obs[id(node)].append((now, now + left))
            node_info.setdefault(id(node), (node.kind, node.level, node.n2, bool(node.children), node.start_time))
            watched.add(node)

    def on_line(state, line):
        for node in [n for n in watched if n.own_done]:
            node_end[id(node)] = state.log_time
            watched.discard(node)

    state, stamps = replay(d, args.log, on_frame, on_line)
    nodes = [{"info": node_info[k], "obs": v, "end": node_end.get(k)} for k, v in node_obs.items()]
    with open(args.out, "w") as f:
        json.dump({"frames": frames, "milestones": {k: stamps[k] for k in MILESTONES if k in stamps},
                   "log_start": state.log_start, "final": work_totals(state), "nodes": nodes}, f)
    print(f"{args.log}: {len(frames)} readings, {len(nodes)} tasks -> {args.out}")


if __name__ == "__main__":
    main()
