#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Resolve selected foundation sources and standalone product fixtures."""
from pathlib import Path


def source_root(root):
    """Use the composed source tree when the build has prepared it."""
    view = Path(root) / 'build/foundation'
    return view if view.is_dir() else Path(root)


def source_path(root, relative):
    """Resolve a selected file while allowing uncomposed test fixtures."""
    selected = source_root(root) / relative
    return selected if selected.is_file() else Path(root) / relative
