#!/usr/bin/env python3
"""Run clipboard peers on a private nested X server, never the user's clipboard."""
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys

server = shutil.which("Xvfb") or shutil.which("Xephyr")
if not server:
    raise SystemExit("Install Xvfb or Xephyr to run isolated clipboard tests.")
args = [server, "-displayfd", "1", "-nolisten", "tcp", "-noreset"]
if Path(server).name == "Xvfb":
    args += ["-screen", "0", "640x480x24"]
else:
    if not os.environ.get("DISPLAY"):
        raise SystemExit("Xephyr needs DISPLAY; use Xvfb for headless testing.")
    args += ["-screen", "640x480"]
child = subprocess.Popen(args, stdout=subprocess.PIPE)
try:
    if not select.select([child.stdout], [], [], 10)[0]:
        raise SystemExit("Nested display did not become ready within 10 seconds.")
    number = child.stdout.readline().strip()
    if not number.isdigit():
        raise SystemExit("Nested display failed to report its display number.")
    env = dict(os.environ, TIKU_CLIPBOARD_TEST_DISPLAY=":" + number.decode())
    binary = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else
              Path(__file__).resolve().parents[2] / "build/tests/test_host_clipboard")
    raise SystemExit(subprocess.run([str(binary)], env=env, timeout=30).returncode)
finally:
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()
