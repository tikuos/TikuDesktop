#!/usr/bin/env python3
"""Reject new dependency leaks while the desktop runtime is extracted."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
TRACKER_SRC = ROOT / "src"
DESKTOP_SRC = ROOT.parent / "desktop" / "src"

X11_LEGACY = set()

HEADLESS = {
    "alert", "attr", "backend_posix", "backend_tiku", "clip", "drag",
    "edit", "frame", "fs", "ident", "image", "inflate", "info",
    "jpeg", "launch", "match", "model", "navigator", "navmenu",
    "nodeicon", "notify", "panel", "poseview", "prefs", "query",
    "scroll", "select", "state", "switcher", "task", "thumb", "types",
    "vdir", "volume",
}

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def tracker_stem(path: Path) -> str:
    stem = path.stem
    prefix = "tiku_trk_"
    return stem[len(prefix):] if stem.startswith(prefix) else stem


def main() -> int:
    errors: list[str] = []

    for path in sorted(DESKTOP_SRC.glob("*.[ch]")):
        for inc in INCLUDE.findall(path.read_text(errors="replace")):
            if inc.startswith("tiku_trk_"):
                errors.append(f"{path}: desktop runtime imports Tracker: {inc}")

    for path in sorted(TRACKER_SRC.glob("*.[ch]")):
        includes = INCLUDE.findall(path.read_text(errors="replace"))
        x11 = [inc for inc in includes if inc.startswith("X11/")]
        if x11 and path.name not in X11_LEGACY:
            errors.append(f"{path}: new direct X11 dependency: {', '.join(x11)}")
        if tracker_stem(path) in HEADLESS:
            ui = [inc for inc in includes
                  if inc in {"tiku_desk_gfx.h", "tiku_desk_font.h",
                             "tiku_desk_ui.h", "tiku_desk_x11.h",
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
