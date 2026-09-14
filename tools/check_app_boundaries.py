#!/usr/bin/env python3
"""Keep application implementation out of the desktop and its public SDK."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def violations(root, shell_sources):
    errors = []
    for source in shell_sources:
        if source.startswith("apps/") or "/apps/" in source:
            errors.append(f"application source linked into a shell: {source}")
    for tree in ("TikuTracker/src", "kits", "apps", "system"):
        for path in (root / tree).rglob("*"):
            if path.suffix not in (".c", ".h", ".inl"):
                continue
            source = path.read_text(encoding="utf-8")
            for included in re.findall(r'^\s*#\s*include\s*[<"]([^">]+)', source, re.M):
                if tree in ("apps", "system") and ("tiku_trk_" in included or "TikuTracker/src" in included):
                    errors.append(f"app imports desktop-private API: {path.relative_to(root)}: {included}")
                if tree != "apps" and (included.startswith("apps/") or "/apps/" in included):
                    errors.append(f"system imports application source: {path.relative_to(root)}: {included}")
            if tree == "TikuTracker/src" and re.search(r'\btiku_app_load\s*\(', source):
                errors.append(f"desktop may not load application modules: {path.relative_to(root)}")
    return errors


def main():
    output = subprocess.check_output(
        ["make", "-s", "--no-print-directory", "print-shell-sources"], cwd=ROOT, text=True)
    errors = violations(ROOT, output.split())
    forbidden = re.compile(r'\b_?(?:tiku_app_load|tiku_reader_\w+|tiku_image_window_\w+|'
                           r'device_utilit\w+|desktop_utilit\w+|tiku_demo_app)\b')
    for name in ("TikuDesktop", "TikuTracker"):
        binary = ROOT / "build/system" / name
        if not binary.exists():
            errors.append(f"build the shipping shells first: {binary.relative_to(ROOT)}")
            continue
        symbols = subprocess.check_output(["nm", str(binary)], text=True)
        for match in sorted(set(forbidden.findall(symbols))):
            errors.append(f"application implementation in {name}: {match}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("application boundaries hold: no private imports, app code or module loader in the shells")
    return 0


if __name__ == "__main__":
    sys.exit(main())
