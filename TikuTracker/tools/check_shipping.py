#!/usr/bin/env python3
"""What ships carries no test infrastructure.

The scripted driver is how nearly every behaviour in this tree is proven,
and it is compiled into a binary that can be pointed at any file on disk.
That belongs to developers, not to a device image, so the shells are built
in two flavours from one source tree (see the Makefile): build/ links a
stub that refuses -script, build/dev/ links the interpreter.

A promise that is not checked is a promise that quietly lapses -- one
Makefile edit putting the engine back into the shipping list would look
exactly like a working build.  This is that check: the script vocabulary
must be ABSENT from what ships and PRESENT in what the harness runs.
"""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SHIPPING = ["build/TikuTracker", "build/TikuDesktop"]
DEVELOPMENT = ["build/dev/TikuTracker", "build/dev/TikuDesktop"]

# Words only the interpreter has any reason to hold.  Ordinary strings
# ("click", "wait") would appear for a hundred innocent reasons; these are
# script ops, spelled the way only the parser spells them.
VOCABULARY = [b"expect-pixel", b"expect-title", b"click-desk",
              b"expect-same-as-redraw", b"menu-pick"]


def words_in(path: Path) -> list[bytes]:
    blob = path.read_bytes()
    return [w for w in VOCABULARY if w in blob]


def main() -> int:
    errors: list[str] = []

    for name in SHIPPING:
        path = ROOT / name
        if not path.exists():
            errors.append(f"{name}: not built, so nothing was checked")
            continue
        found = words_in(path)
        if found:
            spelled = ", ".join(w.decode() for w in found)
            errors.append(f"{name}: SHIPS the script engine ({spelled})")

    for name in DEVELOPMENT:
        path = ROOT / name
        if not path.exists():
            errors.append(f"{name}: not built, so nothing was checked")
            continue
        if not words_in(path):
            errors.append(f"{name}: has NO script engine -- the harness "
                          f"would run scripts against a shell that "
                          f"cannot read them")

    if errors:
        print("shipping boundary violations:")
        for error in errors:
            print(f"  {error}")
        return 1

    print("the shipping shells carry no test infrastructure "
          f"({len(VOCABULARY)} script words looked for, none found)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
