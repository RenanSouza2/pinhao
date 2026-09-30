#!/usr/bin/env python3
"""Split the tree phase's ETA error, at every 5% of the split, into the work
left (the model's over what the rest of the run really did, both priced at
that reading's cost) and the rate (the assumed seconds per unit of work over
what the rest of the run delivered). Reads eta_replay.py's JSON."""

import argparse
import json

from common import speedup


def diag(path):
    r = json.load(open(path))
    t0, final = r["log_start"], r["final"]
    split_end, end = r["milestones"]["binary split solved"], r["milestones"]["display end"]
    rows = []
    for f in r["frames"]:
        if f["rest"] is None or f["phase"] != "splitting":
            continue
        s = f["s"]
        done_final = final["piece"] + final["io"] + sum(v * speedup(s, k) for k, v in final["mul"].items())
        work_true = done_final - f["done"]
        left = split_end - f["t"]
        rows.append(((f["t"] - t0) / (end - t0), (f["t"] + f["rest"] - end) / 60,
                     (f["left"] * f["rate"] - left) / 60, f["left"] / work_true,
                     f["rate"] / (left / work_true), s, f["b"], f["pieces_done"], f["joins_done"], 100 * f["units"]))
    print(f"== {path}")
    print("    %   error  split error  work  rate     s      b  pieces  joins  units%")
    for target in [i / 20 for i in range(1, 20)]:
        row = min(rows, key=lambda x: abs(x[0] - target))
        print("  %3.0f %+6.0f %+10.0f %8.2f %5.2f %5.2f %6.3f %7d %6d %7.1f" % ((100 * row[0],) + row[1:]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json", nargs="+", help="output of eta_replay.py")
    for path in parser.parse_args().json:
        diag(path)


if __name__ == "__main__":
    main()
