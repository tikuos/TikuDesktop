#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Resolve selected foundation sources and standalone product fixtures."""
from pathlib import Path
import os


def source_root(root):
    """Use the composed source tree when the build has prepared it."""
    root=Path(root)
    product=os.environ.get('STOMME_SOURCE_PRODUCT')
    selected=os.environ.get('STOMME_SOURCE_VIEW')
    view=root/'build/foundation'
    if product and selected and Path(product).resolve()==root.resolve():
        candidate=Path(selected)
        view=candidate if candidate.is_absolute() else root/candidate
    return view if view.is_dir() else Path(root)


def source_path(root, relative):
    """Resolve a selected file while allowing uncomposed test fixtures."""
    selected = source_root(root) / relative
    return selected if selected.is_file() else Path(root) / relative
