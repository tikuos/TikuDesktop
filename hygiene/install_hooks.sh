#!/bin/sh
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Install Stomme's commit rules for this TikuDesktop checkout.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
hooks=foundation/hygiene/githooks
[ "$(git -C "$root" rev-parse --show-toplevel)" = "$root" ] || exit 1
[ -x "$root/$hooks/commit-msg" ] && [ -x "$root/foundation/hygiene/check_commit_msg.py" ] || {
    echo "Initialize Stomme with git submodule update --init --recursive" >&2
    exit 1
}
git -C "$root" config --local core.hooksPath "$hooks"
printf 'TikuDesktop commit hooks: %s\n' "$hooks"
