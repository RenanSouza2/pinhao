#!/usr/bin/env python3
"""Check a pinhao digits file against api.pi.delivery.

Checks the last --tail digits, then --samples random offsets (default: until
a mismatch or Ctrl-C).

Usage: ./verify.py [path/to/pi_<size>.txt] [--tail N] [--samples N] [--batch N]
                   [--delay S]
"""

import argparse
import glob
import json
import os
import random
import sys
import time
import urllib.error
import urllib.request

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
RES_DIR = os.path.join(REPO_ROOT, "cache", "res", "dec")

API = "https://api.pi.delivery/v1/pi?start={}&numberOfDigits={}"
API_MAX_DIGITS = 1000
RETRIES = 5


def fetch(start, count):
    url = API.format(start, count)
    for attempt in range(RETRIES):
        try:
            with urllib.request.urlopen(url, timeout=30) as res:
                return json.load(res)["content"]
        except (urllib.error.URLError, TimeoutError, OSError) as err:
            if attempt == RETRIES - 1:
                raise
            wait = 2 ** attempt
            print(f"  fetch {start}+{count} failed ({err}), retrying in {wait}s", flush=True)
            time.sleep(wait)
    raise AssertionError("unreachable")


def read(fp, start, count):
    fp.seek(start)
    return fp.read(count).decode("ascii")


CONTEXT = 20


# returns (digit index, got, expected, index into got) of the first difference
def mismatch(fp, start, count):
    got = read(fp, start, count)
    expected = fetch(start, count)
    for i, (g, e) in enumerate(zip(got, expected)):
        if g != e:
            return start + i, got, expected, i
    if len(got) != len(expected):
        i = min(len(got), len(expected))
        return start + i, got, expected, i
    return None


def fail(size, where, got, expected, i):
    lo = max(0, i - CONTEXT)
    hi = i + CONTEXT
    print(f"\nMISMATCH at digit {where} of {size} ({size - where} from the end)")
    print(f"  file: {got[lo:i]} {got[i:hi]}")
    print(f"  api : {expected[lo:i]} {expected[i:hi]}")
    sys.exit(1)


def check_tail(fp, size, tail):
    begin = max(0, size - tail)
    print(f"tail: digits {begin}..{size - 1}")
    for start in range(begin, size, API_MAX_DIGITS):
        count = min(API_MAX_DIGITS, size - start)
        bad = mismatch(fp, start, count)
        if bad:
            fail(size, *bad)
    print(f"tail: ok ({size - begin} digits)")


# SAMPLES None runs until a mismatch or Ctrl-C
def sample(fp, size, batch, delay, samples):
    batch = min(batch, size)
    reach = 0
    i = 0
    try:
        while samples is None or i < samples:
            if i:
                time.sleep(delay)
            pos = random.randrange(size - batch + 1)
            reach = max(reach, pos)
            print(f"{i:8d} : {pos:15d} | {100 * pos / size:5.1f}% | max {100 * reach / size:5.1f}%")
            bad = mismatch(fp, pos, batch)
            if bad:
                fail(size, *bad)
            i += 1
    except KeyboardInterrupt:
        print(f"\nstopped: {i} samples of {batch} digits ok")
        return
    print(f"samples: ok ({i} of {batch} digits)")


def default_path():
    paths = sorted(glob.glob(os.path.join(RES_DIR, "pi_*.txt")))
    if not paths:
        sys.exit(f"verify.py: no pi_*.txt in {RES_DIR}; pass a path")
    return paths[-1]


def main():
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path", nargs="?", help="digits file (default: largest cache/res/dec/pi_*.txt)")
    parser.add_argument("--tail", type=int, default=1000, help="trailing digits checked first (default 1000)")
    parser.add_argument("--samples", type=int, default=None, help="random samples after the tail (default: until a mismatch or Ctrl-C)")
    parser.add_argument("--batch", type=int, default=100, help=f"digits per random sample, <= {API_MAX_DIGITS} (default 100)")
    parser.add_argument("--delay", type=float, default=1.0, help="seconds between random samples (default 1)")
    args = parser.parse_args()

    if not 1 <= args.batch <= API_MAX_DIGITS:
        parser.error(f"--batch must be in 1..{API_MAX_DIGITS}")
    if args.tail < 0:
        parser.error("--tail must be >= 0")
    if args.samples is not None and args.samples < 0:
        parser.error("--samples must be >= 0")

    path = args.path or default_path()
    with open(path, "rb") as fp:
        size = os.fstat(fp.fileno()).st_size
        if size == 0:
            sys.exit(f"verify.py: {path} is empty")
        print(f"{path}: {size} digits")

        check_tail(fp, size, args.tail)
        sample(fp, size, args.batch, args.delay, args.samples)


if __name__ == "__main__":
    main()
