#!/usr/bin/env python3
"""Score what eta_replay.py saved: the run ETA's error at points through the
run and by quarter, the split alone, the post-split estimate, and each task's
own ETA by kind. Errors are the predicted end minus the real one, so negative
is early."""

import argparse
import collections
import json
import statistics


def quarters(err):
    q = [[abs(e) for p, e in err if k / 4 <= p < (k + 1) / 4] for k in range(4)]
    return "/".join(f"{statistics.median(x):.1f}" if x else "-" for x in q)


def task_kind(kind, n2, has_children):
    if not has_children:
        return "piece"
    if kind != "SPAN":
        return kind.lower()
    return "span <=25" if n2 <= 25 else ("span 26-28" if n2 <= 28 else "span 29+")


def score(path):
    r = json.load(open(path))
    ms, t0 = r["milestones"], r["log_start"]
    end, split_end = ms["display end"], ms["binary split solved"]
    fr = [f for f in r["frames"] if f["rest"] is not None]
    split = [f for f in fr if f["phase"] == "splitting"]
    print(f"== {path}: run {(end - t0) / 3600:.2f} h, split {(split_end - t0) / 3600:.2f} h, "
          f"divide {ms['divided'] - ms['dividing']:.0f} s, display {end - ms['display begin']:.0f} s, "
          f"first ETA at {(fr[0]['t'] - t0) / 60:.0f} min")

    err = [((f["t"] - t0) / (end - t0), (f["t"] + f["rest"] - end) / 60) for f in fr]
    at = lambda p: min(err, key=lambda e: abs(e[0] - p))[1]
    print("  error at     ", "  ".join(f"{int(p * 100)}%: {at(p):+.0f}" for p in (.02, .1, .25, .5, .75, .9, .95)), "min")
    moves = [abs(b[1] - a[1]) for a, b in zip(err, err[1:])]
    print(f"  by quarter    {quarters(err)} min, early {100 * sum(e < 0 for _, e in err) / len(err):.0f}%, "
          f"median move {statistics.median(moves):.2f} min")
    split_err = [((f["t"] - t0) / (split_end - t0), (f["t"] + f["rest"] - f["post"] - split_end) / 60) for f in split]
    print(f"  split only    {quarters(split_err)} min")
    print(f"  post-split    priced {split[0]['post']:.0f} s at the first reading, {split[-1]['post']:.0f} s at the "
          f"split's end; took {end - split_end:.0f} s")

    groups = collections.defaultdict(list)
    for n in r["nodes"]:
        if n["end"] is None:
            continue
        kind, _, n2, has_children, start = n["info"]
        first_err = n["obs"][0][1] - n["end"]
        groups[task_kind(kind, n2, has_children)].append(
            (statistics.median(abs(p - n["end"]) for _, p in n["obs"]), first_err, n["end"] - start))
    print("  task ETAs     kind         tasks  median time  median |error|  first reading |error|/time  early")
    for k in sorted(groups):
        g = groups[k]
        rel = statistics.median(abs(e) / dur for _, e, dur in g if dur > 0)
        print(f"                {k:11s} {len(g):6d} {statistics.median(dur for _, _, dur in g):10.0f} s "
              f"{statistics.median(m for m, _, _ in g):12.1f} s {100 * rel:21.0f}% {100 * sum(e < 0 for _, e, _ in g) / len(g):8.0f}%")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json", nargs="+", help="output of eta_replay.py")
    for path in parser.parse_args().json:
        score(path)


if __name__ == "__main__":
    main()
