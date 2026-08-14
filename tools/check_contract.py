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
    header = re.compile(r"^#### ([A-Z]+-\d+) · (\w+) · (\w+)(?:.*)$", re.M)
    heads = list(header.finditer(text))
    rows = [(m.group(1), m.group(2), m.group(3)) for m in heads]
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
    unresolved = ("**BLOCKED:**", "**SUBSYSTEM:**", "**WAIVER CANDIDATE:**",
                  "**UNCONFIRMED:**", "**TODO:**")
    for i, m in enumerate(heads):
        status = m.group(3)
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        body = text[m.end():end]
        rid = m.group(1)
        if status == "partial" and "**MISSING:**" not in body:
            print("contract: partial row %s has no **MISSING:** reason" % rid)
            bad += 1
        if status == "differs" and "**PORT:**" not in body:
            print("contract: differs row %s has no **PORT:** alternative" % rid)
            bad += 1
        if status == "done" and any(mark in body for mark in unresolved):
            print("contract: done row %s still has unresolved evidence" % rid)
            bad += 1
    if bad:
        return 1
    print("  [PASS] contract   %d rows, no duplicates: %s" %
          (len(rows), dict(verdicts)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "FEATURES.md"))
