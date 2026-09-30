#!/usr/bin/env python3
"""Read probe.py's JSON and print two tables. The warm-up: the run ETA's error
over the first minutes against what its cost fit had seen. The split: at every
5%, the work the ETA still priced against what the rest of the run really did,
in thread-hours, by piece, multiplication and load/write/add; the rate as
assumed over delivered; and, since the reading before, the threads booked and
working, the work done per second, and the launch halts by reason."""

import argparse
import json

from common import speedup


def warmup(r, minutes):
    t0, end = r["t0"], r["ms"]["display end"]
    print("  warm-up   min  error  muls  pieces  joins  thread counts  size spread      b     s")
    for f in r["frames"][:minutes]:
        e = "     -" if f["rest"] is None else f"{(f['t'] + f['rest'] - end) / 60:+6.0f}"
        print(f"          {(f['t'] - t0) / 60:4.0f} {e} {f['n_mul']:5d} {f['n_piece']:7d} {f['joins']:6d} "
              f"{f['n_thr']:14d} {f['spread']:11.1f} {f['b']:6.3f} {f['s']:5.2f}")


def split(r):
    t0, final = r["t0"], r["final"]
    split_end, end = r["ms"]["binary split solved"], r["ms"]["display end"]
    fr = [f for f in r["frames"] if f.get("parts")]
    s_final = fr[-1]["s"]

    def done_at(acc, s):
        return acc["piece"] + acc["io"] + sum(v * speedup(s, k) for k, v in acc["mul"].items())

    print("  split     %  error | left, model/real thread-hours: piece   mult.     i/o    | rate | booked working done/s  halts mem/thr")
    prev = None
    for target in [i / 20 for i in range(1, 20)]:
        f = min(fr, key=lambda x: abs((x["t"] - t0) / (split_end - t0) - target))
        s = f["s"]
        real_piece = final["piece"] - f["acc"]["piece"]
        real_io = final["io"] - f["acc"]["io"]
        real_mul = sum((final["mul"][k] - f["acc"]["mul"].get(k, 0)) * speedup(s, k) for k in final["mul"])
        work_real = real_piece + real_io + real_mul
        rate = f["rate"] / ((split_end - f["t"]) / work_real)
        parts = f["parts"]
        cells = "  ".join(f"{parts.get(k, 0) / 3600:5.1f}/{real / 3600:<5.1f}"
                          for k, real in (("piece", real_piece), ("mul", real_mul), ("io", real_io)))
        line = f"       {100 * target:5.0f} {(f['t'] + f['rest'] - end) / 60:+6.0f} |                            {cells} | {rate:4.2f}"
        if prev:
            span = f["span"] - prev["span"]
            booked = (f["booked_time"] - prev["booked_time"]) / span
            working = (f["thread_time"] - prev["thread_time"]) / span
            per_s = (done_at(f["acc"], s_final) - done_at(prev["acc"], s_final)) / (f["t"] - prev["t"])
            halts = [f["halts"].get(k, 0) - prev["halts"].get(k, 0) for k in ("mem_launch", "threads")]
            line += f" | {booked:6.1f} {working:7.1f} {per_s:6.1f}  {halts[0]:5d}/{halts[1]}"
        print(line)
        prev = f


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json", nargs="+", help="output of probe.py")
    parser.add_argument("--minutes", type=int, default=45, help="warm-up rows (default 45)")
    args = parser.parse_args()
    for path in args.json:
        r = json.load(open(path))
        print(f"== {path}")
        warmup(r, args.minutes)
        split(r)


if __name__ == "__main__":
    main()
