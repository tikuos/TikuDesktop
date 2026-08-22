#!/usr/bin/env python3
"""
Find the words still written in the code.

A DEVELOPMENT tool.  The interface's words are moving into strings/en.xml,
a key at a time; this counts the UI-looking string literals not yet behind
a tiku_str() key, so what is left to move is a number rather than a
guess.

It is a HEURISTIC, deliberately broad: a literal that begins with a
capital or holds a space, is not a path, a format string, an all-caps
identifier or a key, and is not already an argument to tiku_str().  It
over-counts (some are log lines or internal) and under-counts (lower-case
labels), so read it as a direction of travel, not a total.

  tools/check_strings.py            # count, and the files worst first
  tools/check_strings.py --list     # every remaining literal, with place

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""

import os
import re
import sys

LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
SKIP = re.compile(r'^(-+|/|\\x|%[.\-0-9]*[sdxufg]|[A-Z0-9_]{2,}$|'
                  r'[a-z_]+/[a-z]|_trk/|inode/|[.,:;|]+|\s*)$')


def ui_word(s):
    if not s or SKIP.match(s):
        return False
    # begins with a capital, or holds a space between letters
    if not (re.match(r'[A-Z]', s) or re.search(r'[A-Za-z] [A-Za-z]', s)):
        return False
    if '/' in s and ' ' not in s:      # a path, not a sentence
        return False
    return re.search(r'[A-Za-z]{2}', s) is not None


def scan(path):
    hits = []
    for i, line in enumerate(open(path, encoding='utf-8',
                                  errors='replace').read().splitlines(), 1):
        # a line already routed through the catalogue is done
        routed = 'tiku_str(' in line
        for m in LITERAL.finditer(line):
            s = m.group(1)
            if routed and ('"%s"' == '"' + s + '"' or s == '%s'):
                continue
            if routed:
                continue          # the whole line is a lookup: skip it
            if ui_word(s):
                hits.append((i, s))
    return hits


def main():
    show = '--list' in sys.argv
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
    per_file = {}
    total = 0
    for base, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith('.c'):
                continue
            hits = scan(os.path.join(base, name))
            if hits:
                rel = os.path.relpath(os.path.join(base, name),
                                      os.path.join(root, '..'))
                per_file[rel] = hits
                total += len(hits)

    print("UI-looking literals not yet behind a key: %d (heuristic)" % total)
    print("across %d files\n" % len(per_file))
    for rel in sorted(per_file, key=lambda r: -len(per_file[r])):
        print("  %3d  %s" % (len(per_file[rel]), rel))
        if show:
            for ln, w in per_file[rel]:
                print("         %d: %s" % (ln, w[:58]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
