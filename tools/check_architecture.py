#!/usr/bin/env python3
"""
Reject new dependency leaks in Tracker itself.

The KITS have their own guard -- applications/kits/tools/check_kits.py --
which holds the table of what each kit may draw on.  This one is about
the application above them: that no kit ever learns Tracker's name, and
that the modules which must stay drawable-nowhere keep away from the
Interface kit.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
TRACKER_SRC = ROOT / "src"
KITS = ROOT.parent / "kits"

X11_LEGACY = set()

# Tracker's own modules that must draw nothing.  The list is shorter than
# it was because the ones that had no business drawing in the FIRST place
# -- nodes, attributes, queries, volumes, decoders -- are kits now, and a
# kit that reached the Interface kit is refused by check_kits.py before
# this ever runs.
HEADLESS = {
    "alert", "clip", "drag", "edit", "frame", "info", "launch", "match",
    "navigator", "navmenu", "nodeicon", "panel", "poseview", "prefs",
    "scroll", "select", "switcher", "task", "types",
}

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def tracker_stem(path: Path) -> str:
    stem = path.stem
    prefix = "tiku_trk_"
    return stem[len(prefix):] if stem.startswith(prefix) else stem


def main() -> int:
    errors: list[str] = []

    flat_sources = sorted(TRACKER_SRC.glob("*.[ch]"))
    if flat_sources:
        names = ", ".join(path.name for path in flat_sources)
        errors.append(f"flat Tracker sources must live in a subsystem: {names}")

    for legacy in ("tiku_trk_win.c", "tiku_trk_win.h"):
        if any(TRACKER_SRC.rglob(legacy)):
            errors.append(f"{legacy}: generic windows belong to the Interface kit")

    # No kit may know the application standing on it.  Storage and
    # Translation moved out of Tracker and kept their tiku_trk_ names for
    # now, so the test is by PATH: a kit may include a kit header, never
    # one that is still Tracker's.
    kit_headers = {p.name for p in KITS.rglob("*.h")}
    for path in sorted(KITS.rglob("*.[ch]")):
        for inc in INCLUDE.findall(path.read_text(errors="replace")):
            if inc.startswith("tiku_trk_") and inc not in kit_headers:
                errors.append(f"{path}: a kit imports Tracker: {inc}")

    for path in sorted(TRACKER_SRC.rglob("*.[ch]")):
        includes = INCLUDE.findall(path.read_text(errors="replace"))
        x11 = [inc for inc in includes if inc.startswith("X11/")]
        if x11 and path.name not in X11_LEGACY:
            errors.append(f"{path}: new direct X11 dependency: {', '.join(x11)}")
        if tracker_stem(path) in HEADLESS:
            ui = [inc for inc in includes
                  if inc in {"tiku_gfx.h", "tiku_font.h",
                             "tiku_ui.h", "tiku_host.h",
                             "tiku_trk_gfx_dep.h"}]
            if ui:
                errors.append(f"{path}: headless module imports UI: {', '.join(ui)}")

    if errors:
        print("architecture boundary violations:")
        for error in errors:
            print(f"  {error}")
        return 1

    print("architecture boundaries hold "
          f"({len(X11_LEGACY)} explicit X11 migration exceptions remain)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
