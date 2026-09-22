#!/bin/sh
#
# TikuTracker.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# sweep_shells.sh - what the single-window shell does with the other one's
# scripts.
#
# Both front-ends are the product, and a behaviour proven in one is not
# proven in the other.  This runs every desktop-only script against
# build/dev/TikuTracker as well and reports how far each gets.  It is
# NOT part of `make test`: most of these scripts are red there by
# construction -- what it measures is HOW red, and which of them are red
# for a reason the contract accepts (several windows, the Deskbar, the
# backdrop) rather than because the command layer is not shared.
#
# SPDX-License-Identifier: Apache-2.0
set -u
here=$(dirname "$0")
root=$here/..
cd "$root" || exit 1

total=0
clean=0
for s in tests/integration/shell/*.script; do
    grep -q '^# shell: desktop' "$s" || continue
    n=$(basename "$s")
    cp "$s" /tmp/sweep_keep.script
    sed -i '1s/# shell: desktop/# shell: both/' "$s"
    out=$(TRK_SHELL_ONLY="$n" sh tests/integration/shell/run.sh 2>&1 |
          sed -n '/(tracker)/,/^$/p')
    fails=$(printf '%s' "$out" | grep -c '\[FAIL\]')
    passes=$(printf '%s' "$out" | grep -c '\[PASS\]')
    cp /tmp/sweep_keep.script "$s"
    total=$((total + 1))
    [ "$fails" -eq 0 ] && clean=$((clean + 1))
    printf '%-24s tracker: %s passed, %s failed\n' "$n" "$passes" "$fails"
done
printf '\n%d desktop scripts, %d of them clean under the single-window shell\n' \
    "$total" "$clean"
