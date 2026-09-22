#!/bin/sh
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Resolve runtime resources relative to the launcher, including through aliases.
# SPDX-License-Identifier: Apache-2.0
set -eu
launcher=$0
while [ -L "$launcher" ]; do
    target=$(readlink "$launcher")
    case "$target" in
        /*) launcher=$target ;;
        *) launcher=$(dirname "$launcher")/$target ;;
    esac
done
runtime=$(CDPATH= cd "$(dirname "$launcher")" && pwd)
name=$(basename "$launcher")
: "${TIKU_APPS:=$runtime/apps}"
: "${TIKU_HELP:=$runtime/assets/help}"
: "${TIKU_STRINGS:=$runtime/assets/strings}"
export TIKU_APPS TIKU_HELP TIKU_STRINGS
exec "$runtime/system/$name" "$@"
