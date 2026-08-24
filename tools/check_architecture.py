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
import subprocess
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

# The one structural fact the tree cannot show: which sources belong to
# ONE front-end.  Nearly everything is linked into both binaries -- the
# two shells are one product built two ways -- and the whole remainder
# is listed here.  app is the single-window composition; the navigator
# is its Back/Forward/Up, which the spatial shell deliberately has no
# counterpart for (FEATURES.md holds the waiver).  Growing this set is
# allowed but must be MEANT: a file that joins it by accident has been
# dropped from one binary's link line, which is precisely the quiet
# breakage this check exists to catch.
TRACKER_ONLY = {
    "src/shell/tiku_trk_app.c",
    "src/ui/tiku_trk_navigator.c",
}


def ownership() -> list[str]:
    """The link lists as MAKE resolves them, not as a regex reads them."""
    out = subprocess.run(
        ["make", "-s", "-C", str(ROOT), "print-ownership"],
        capture_output=True, text=True, check=True).stdout
    sets: dict[str, set[str]] = {}
    for line in out.splitlines():
        label, _, rest = line.partition(":")
        sets[label] = set(rest.split())
    errors: list[str] = []

    tracker_only = sets["TRACKER"] - sets["DESKTOP"]
    if tracker_only != TRACKER_ONLY:
        gained = sorted(tracker_only - TRACKER_ONLY)
        lost = sorted(TRACKER_ONLY - tracker_only)
        for f in gained:
            errors.append(
                f"{f}: linked into TikuTracker alone -- dropped from the "
                "desktop, or meant to be declared in TRACKER_ONLY?")
        for f in lost:
            errors.append(
                f"{f}: declared TikuTracker-only but no longer is")

    # Every source is somebody's: linked into a front-end, part of the
    # script engine, or named by a test rule.  A .c file nothing names is
    # how code keeps compiling in nobody's binary.
    named = sets["TRACKER"] | sets["DESKTOP"] | sets["ENGINE"]
    makefile = (ROOT / "Makefile").read_text()
    for path in sorted(TRACKER_SRC.rglob("*.c")):
        rel = str(path.relative_to(ROOT))
        if rel not in named and rel not in makefile:
            errors.append(f"{rel}: no binary, engine, or test names it")
    return errors


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

    errors.extend(ownership())

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
