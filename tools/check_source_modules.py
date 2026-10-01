#!/usr/bin/env python3
"""Keep migrated application families as separately compiled, named modules.

The inventory includes .inl and Objective-C files across apps, kits and system.
Generated font/icon data is excluded from size rankings, not hand-edited or split.
Legacy families remain visible in the report without inheriting new style rules.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FAMILIES = {"ide": "tiku_ide_", "editor": "tiku_editor_",
            "reader": "tiku_reader_", "bench": "tiku_bench_",
            "console": "tiku_console_"}
SUFFIXES = {".c", ".h", ".inl", ".m"}
GENERATED = {"kits/interface/tiku_font_data.h", "kits/translation/tiku_icons.c"}
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.M)
MODELS = {"apps/ide/tiku_ide_project.c", "apps/reader/tiku_reader_document.c"}
UI_HEADERS = {"tiku_gfx.h", "tiku_window.h", "tiku_ui.h", "tiku_client.h",
              "tiku_app.h", "tiku_doc_paint.h"}


def includes(text):
    """Ignore commented-out includes without consuming real quoted paths."""
    text = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                  lambda m: m[0] if m[0].startswith('"') else
                  re.sub(r'[^\n]', ' ', m[0]), text)
    return INCLUDE.findall(text)


def sources(root):
    return sorted(p for tree in ("apps", "kits", "system")
                  for p in (root / tree).rglob("*")
                  if p.is_file() and p.suffix in SUFFIXES)


def violations(root):
    errors = []
    for family, prefix in FAMILIES.items():
        directory = root / "apps" / family
        for p in directory.rglob("*"):
            if not p.is_file() or p.suffix not in SUFFIXES:
                continue
            rel = p.relative_to(root).as_posix()
            if not p.name.startswith(prefix):
                errors.append(f"{rel}: use {prefix}<responsibility>{p.suffix}")
            if p.suffix == ".inl":
                errors.append(f"{rel}: compile implementation as a .c module")
            for inc in includes(p.read_text()):
                if Path(inc).suffix in {".c", ".m", ".inl"}:
                    errors.append(f"{rel}: includes implementation {inc}")
    # Tests must exercise the actual linked modules, not a second amalgamation.
    prefixes = tuple(FAMILIES.values()) + ("tiku_edit.", "tiku_edit_",
                                         "tiku_bench.", "tiku_console.")
    for p in (root / "tests").rglob("*.c"):
        for inc in includes(p.read_text()):
            name = Path(inc)
            if name.suffix in {".c", ".inl"} and name.name.startswith(prefixes):
                errors.append(f"{p.relative_to(root)}: link {inc}, do not include it")
    # Models may include their own narrow headers, but not reach UI via a header.
    for rel in sorted(MODELS):
        pending, seen = [root / rel], set()
        while pending:
            p = pending.pop()
            if p in seen or not p.is_file():
                continue
            seen.add(p)
            for inc in includes(p.read_text()):
                if Path(inc).name.endswith("_private.h") or Path(inc).name in UI_HEADERS:
                    errors.append(f"{rel}: model reaches UI/state through {inc}")
                local = (p.parent / inc).resolve()
                if local.is_file():
                    pending.append(local)
    return errors


def report(root):
    files = sources(root)
    fragments = sum(p.suffix == ".inl" for p in files)
    print(f"source inventory: {len(files)} C/header/Objective-C/fragments, "
          f"including {fragments} .inl files")
    rows = sorted(((len(p.read_text().splitlines()), p.relative_to(root).as_posix())
                   for p in files if p.relative_to(root).as_posix() not in GENERATED),
                  reverse=True)
    for count, path in rows[:15]:
        print(f"  {count:5d}  {path}")
    print("  Generated font/icon data excluded from this size ranking.")


def main():
    errors = violations(ROOT)
    if "--report" in sys.argv:
        report(ROOT)
    for error in errors:
        print("  " + error)
    print(f"source modules: {len(FAMILIES)} application families, "
          f"{len(MODELS)} independent models; {len(errors)} violations")
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
