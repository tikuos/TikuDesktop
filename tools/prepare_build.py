#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Create build directories and migrate compatibility paths without overwrites."""
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[1]


def relocate(old, new):
    """Move a generated path only when its destination is unoccupied."""
    if not old.exists() or old.is_symlink():
        return
    if new.exists() or new.is_symlink():
        raise RuntimeError(f"Both {old} and {new} exist; preserve one before building")
    new.parent.mkdir(parents=True, exist_ok=True)
    old.rename(new)


def link(path, target):
    """Keep aliases relative so a build tree remains relocatable."""
    relative = os.path.relpath(target, path.parent)
    if path.is_symlink():
        if os.readlink(path) == relative:
            return
        path.unlink()
    elif path.exists():
        raise RuntimeError(f"Refusing to replace non-symlink {path}")
    path.symlink_to(relative)


def unlink_alias(path):
    """Drop an alias an earlier layout left; a real file or tree stays."""
    if path.is_symlink():
        path.unlink()


# The launchers people type: the one alias layer the build root keeps.
LAUNCHERS = ('TikuDesktop', 'tiku-desk')

# Aliases an earlier layout put beside them, for scripts since moved to
# the canonical paths, and the standalone Tracker's launcher, which is
# no product.  Nothing reads them; they are removed on sight.
RETIRED = ('system', 'apps', 'gen', 'pic', 'icon-export',
           'TikuTracker', 'runtime/TikuTracker') + tuple(
    f'libtiku_{kit}.a' for kit in ('support', 'device', 'storage',
                                   'interface', 'translation',
                                   'application', 'media'))


def prepare(root=ROOT):
    """Prepare the canonical layout; the root holds the launchers alone."""
    build = root / 'build'
    build.mkdir(exist_ok=True)
    for old, new in (('system', 'runtime/system'), ('apps', 'runtime/apps'),
                     ('gen', 'generated'), ('pic', 'obj/pic')):
        relocate(build / old, build / new)
    for name in ('runtime/system', 'runtime/apps', 'lib/pic', 'generated',
                 'obj/pic', 'tests', 'tools', 'dev', 'examples'):
        (build / name).mkdir(parents=True, exist_ok=True)
    for kit in ('support', 'device', 'storage', 'interface', 'translation',
                'application', 'media'):
        name = f'libtiku_{kit}.a'
        relocate(build / name, build / 'lib' / name)
        relocate(build / 'obj/pic' / name, build / 'lib/pic' / name)
    relocate(build / 'icon-export', build / 'tools/icon-export')
    for name in RETIRED:
        unlink_alias(build / name)
    for name in LAUNCHERS:
        link(build / name, build / 'runtime' / name)


if __name__ == '__main__':
    prepare()
