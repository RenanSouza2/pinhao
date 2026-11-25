#!/usr/bin/env python3
"""Check a pinhao digits file against api.pi.delivery.

Checks the last --tail digits the precision fixes, then --samples random
offsets (default: until a mismatch or Ctrl-C). The size comes from the file
name; digits past it are skipped.

Usage: ./verify.py [path/to/pi_<size>.txt] [--tail N] [--samples N] [--batch N]
                   [--delay S]
"""

import argparse
import glob
import json
import math
import os
import random
import re
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


# "+ 3.1415... * 10 ^ 0", as flt_num_write_dec_threads writes it. Digit k is
# the API's digit k: the integer part, then the fraction.
class Digits:
    def __init__(self, fp, path):
        self.fp = fp
        end = os.fstat(fp.fileno()).st_size
        head = fp.read(64).decode("ascii")
        if not head.startswith(("+ ", "- ")) or "." not in head:
            sys.exit(f"verify.py: {path} does not start with a signed decimal")
        dot = head.index(".")
        self.int_part = head[2:dot]
        self.frac_begin = dot + 1

        back = min(64, end)
        fp.seek(end - back)
        tail = fp.read(back).decode("ascii")
        star = tail.rfind(" * 10 ^ ")
        if star < 0:
            sys.exit(f"verify.py: {path} has no \" * 10 ^ \" suffix")
        exponent = tail[star + len(" * 10 ^ "):].strip()
        if exponent != "0":
            sys.exit(f"verify.py: {path} has exponent {exponent}, not 0")
        self.frac_len = end - back + star - self.frac_begin

    def read(self, start, count):
        n_int = len(self.int_part)
        out = self.int_part[start:start + count]
        frac_start = max(start, n_int) - n_int
        frac_count = count - len(out)
        if frac_count > 0:
            self.fp.seek(self.frac_begin + frac_start)
            out += self.fp.read(frac_count).decode("ascii")
        return out


# fraction digits fixed by all but the last limb, as fxd_dec_digits in araucaria
def trusted_frac(limbs):
    if limbs < 3:
        return 0
    return int((limbs - 2) * 64 * math.log10(2))


CONTEXT = 20


# returns (digit index, got, expected, index into got) of the first difference
def mismatch(digits, start, count):
    got = digits.read(start, count)
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


def check_tail(digits, size, tail):
    begin = max(0, size - tail)
    print(f"tail: digits {begin}..{size - 1}")
    for start in range(begin, size, API_MAX_DIGITS):
        count = min(API_MAX_DIGITS, size - start)
        bad = mismatch(digits, start, count)
        if bad:
            fail(size, *bad)
    print(f"tail: ok ({size - begin} digits)")


# SAMPLES None runs until a mismatch or Ctrl-C
def sample(digits, size, batch, delay, samples):
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
            bad = mismatch(digits, pos, batch)
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
    name = re.fullmatch(r"pi_(\d+)\.txt", os.path.basename(path))
    if not name:
        sys.exit(f"verify.py: {path} is not named pi_<size>.txt, so its precision is unknown")
    limbs = int(name.group(1))

    with open(path, "rb") as fp:
        digits = Digits(fp, path)
        frac = min(digits.frac_len, trusted_frac(limbs))
        size = len(digits.int_part) + frac
        print(f"{path}: {size} digits checked, {digits.frac_len - frac} past the precision skipped")

        check_tail(digits, size, args.tail)
        sample(digits, size, args.batch, args.delay, args.samples)


if __name__ == "__main__":
    main()
