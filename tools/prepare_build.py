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


def prepare(root=ROOT):
    """Prepare the canonical layout and aliases used by existing launchers."""
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
        link(build / name, build / 'lib' / name)
    relocate(build / 'icon-export', build / 'tools/icon-export')
    for old, new in (('system', 'runtime/system'), ('apps', 'runtime/apps'),
                     ('gen', 'generated'), ('pic', 'lib/pic'),
                     ('icon-export', 'tools/icon-export'),
                     ('TikuDesktop', 'runtime/TikuDesktop'),
                     ('TikuTracker', 'runtime/TikuTracker'),
                     ('tiku-desk', 'runtime/tiku-desk')):
        link(build / old, build / new)


if __name__ == '__main__':
    prepare()
