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
        if source.startswith(("apps/", "examples/")) or "/apps/" in source:
            errors.append(f"application source linked into a shell: {source}")
    for tree in ("system", "kits", "apps", "examples"):
        scan_root = root / "build/foundation" if (root / "build/foundation" / tree).is_dir() else root
        for path in (scan_root / tree).rglob("*"):
            if path.suffix not in (".c", ".h", ".inl"):
                continue
            source = path.read_text(encoding="utf-8")
            relative = path.relative_to(scan_root).as_posix()
            host = relative.startswith(("system/runner/", "system/supervisor/"))
            shell = tree == "system" and not host
            for included in re.findall(r'^\s*#\s*include\s*[<"]([^">]+)', source, re.M):
                if (tree in ("apps", "examples") or host) and ("tiku_trk_" in included or "system/" in included):
                    errors.append(f"app imports desktop-private API: {path.relative_to(scan_root)}: {included}")
                app_include = included.startswith("apps/") or (path.parent / included).resolve().is_relative_to((scan_root / "apps").resolve())
                if tree != "apps" and app_include:
                    errors.append(f"system imports application source: {path.relative_to(scan_root)}: {included}")
            if shell and re.search(r'\btiku_app_load\s*\(', source):
                errors.append(f"desktop may not load application modules: {path.relative_to(scan_root)}")
    return errors


def main():
    output = subprocess.check_output(
        ["make", "-s", "--no-print-directory", "print-shell-sources"], cwd=ROOT, text=True)
    errors = violations(ROOT, output.split())
    forbidden = re.compile(r'\b_?(?:tiku_app_load|tiku_reader_\w+|tiku_image_window_\w+|'
                           r'tiku_circuitview_\w+|device_utilit\w+|desktop_utilit\w+|tiku_demo_app)\b')
    # The product desktop, and the Tracker where it now lives: the
    # harness's development flavour, the only one built.
    for name, place in (("TikuDesktop", "build/runtime/system"),
                        ("TikuTracker", "build/dev")):
        binary = ROOT / place / name
        if not binary.exists():
            errors.append(f"build the shells first: {binary.relative_to(ROOT)}")
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
