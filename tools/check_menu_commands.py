#!/usr/bin/env python3
#
# TikuTracker.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_menu_commands.py - every menu item a user can pick must do something.
#
# A command reaches the user through the menu tables and is carried out by a
# shell or a controller.  Nothing links the two, so an item can be offered,
# enabled, given a keyboard shortcut and dispatched into a switch that has no
# case for it -- which looks exactly like a working menu until it is picked.
#
# SPDX-License-Identifier: Apache-2.0

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

MENUS = ROOT / "system/shared/ui/tiku_trk_cwmenu.c"
ENABLE = ROOT / "system/shared/ui/tiku_trk_shortcuts.c"
# Each shell is one composition over the files in its directory, and a
# command may be dispatched from any of them.
DISPATCH = sorted(str(p.relative_to(ROOT))
                  for d in ("system/desktop", "system/tracker")
                  for p in (ROOT / d).glob("*.c")) + [
    "system/shared/ui/commands/tiku_trk_folder_file_actions.c",
    "system/shared/ui/commands/tiku_trk_folder_actions.c",
    "system/shared/ui/commands/tiku_trk_folder_menu.c",
    "system/shared/ui/commands/tiku_trk_launch_controller.c",
    "system/shared/ui/commands/tiku_trk_drop_controller.c",
]

# Picking one of these is meant to do nothing, and the reason is not that
# nobody got to it.
INERT = {
    "TIKU_TRK_CMD_CANCEL":
        "the way out of the drop menu: dismissing IS the action",
}


def offered(text):
    """Commands added to a menu as an item a user can pick."""
    return set(re.findall(r"ADD\([^,]+,\s*(TIKU_TRK_CMD_[A-Z0-9_]+)\s*\)",
                          text))


def disabled_on_add(text):
    """Commands the menu adds and then greys: offered as a statement, not a
    choice -- Name is always a column, and Open with says so when nothing
    can open the thing."""
    return set(re.findall(
        r"tiku_menu_enable\([^,]+,\s*(TIKU_TRK_CMD_[A-Z0-9_]+)\s*,\s*0\)",
        text))


def never_enabled(text):
    """Commands whose enablement rule is an unconditional no."""
    out, pending = set(), []
    for line in text.splitlines():
        m = re.match(r"\s*case\s+(TIKU_TRK_CMD_[A-Z0-9_]+)\s*:", line)
        if m:
            pending.append(m.group(1))
            continue
        if not pending:
            continue
        if re.match(r"\s*return\s+0\s*;", line):
            out |= set(pending)
        if line.strip():
            pending = []
    return out


def dispatched(texts):
    """Commands some shell or controller carries out."""
    out = set()
    for text in texts:
        out |= set(re.findall(r"case\s+(TIKU_TRK_CMD_[A-Z0-9_]+)\s*:", text))
        out |= set(re.findall(
            r"command\s*==\s*(TIKU_TRK_CMD_[A-Z0-9_]+)", text))
        out |= set(re.findall(
            r"(TIKU_TRK_CMD_[A-Z0-9_]+)\s*==\s*command", text))
    return out


def main():
    menus = MENUS.read_text()
    items = offered(menus)
    inert = (never_enabled(ENABLE.read_text()) | set(INERT) |
             disabled_on_add(menus))
    done = dispatched([(ROOT / f).read_text() for f in DISPATCH])
    dead = sorted(c for c in items if c not in done and c not in inert)

    print("menu commands: %d offered, %d inert by rule, %d carried out"
          % (len(items), len(items & inert), len(items & done)))
    for c in dead:
        print("  [FAIL] menu     %s is offered and enabled, and nothing "
              "carries it out" % c)
    if dead:
        print("\nFAILURES")
        return 1
    print("  [PASS] menu      every item a user can pick reaches a handler")
    return 0


if __name__ == "__main__":
    sys.exit(main())
