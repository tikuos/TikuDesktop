#!/usr/bin/env python3
#
# The new Tracker for TikuOS.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_contract.py - the contract's own integrity.
#
# A row list that silently doubles, or loses a row, is worse than one that
# fails: every count drawn from it afterwards is wrong and nothing says so.
# This is the check that would have caught an editing script appending the
# file to itself.
#
# SPDX-License-Identifier: Apache-2.0

import collections
import re
import sys

EXPECT = 889                    # the extracted row count; only the user
                                # may change it, by waiving or adding rows

def main(path):
    text = open(path, encoding="utf-8").read()
    rows = re.findall(r"^#### ([A-Z]+-\d+) · (\w+) · (\w+)", text, re.M)
    bad = 0

    if len(rows) != EXPECT:
        print("contract: %d rows, expected %d" % (len(rows), EXPECT))
        bad += 1
    dup = [r for r, n in collections.Counter(i for i, _, _ in rows).items()
           if n > 1]
    if dup:
        print("contract: duplicated rows: %s" % " ".join(sorted(dup)[:8]))
        bad += 1
    if text.count("# FEATURES.md — the Tracker fidelity contract") != 1:
        print("contract: the file contains more than one copy of itself")
        bad += 1
    verdicts = collections.Counter(v for _, _, v in rows)
    for v in verdicts:
        if v not in ("done", "partial", "open", "differs", "WAIVED"):
            print("contract: unknown verdict %r" % v)
            bad += 1
    if bad:
        return 1
    print("  [PASS] contract   %d rows, no duplicates: %s" %
          (len(rows), dict(verdicts)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "FEATURES.md"))
