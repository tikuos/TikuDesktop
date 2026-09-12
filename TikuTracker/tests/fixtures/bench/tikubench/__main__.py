"""Fake test process: never imports hardware code or opens a device."""
import os
import sys
import time
import signal

key = sys.argv[2]
print("ARGV " + repr(sys.argv[1:]), flush=True)
mode = os.getenv("TIKU_BENCH_TEST_MODE", "normal")
if mode == "wait":
    print("Waiting for input", flush=True)
    assert sys.stdin.readline().strip() == "continue"
elif mode == "hang":
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    print("READY", flush=True)
    time.sleep(60)
elif mode == "empty":
    sys.exit(0)
cid = "common" if key == "kernel" else "other"
if "--only" in sys.argv:
    cid = sys.argv[sys.argv.index("--only") + 1].split(",")[0]
print(f"[TC:END] {cid} pass=3 fail=0", end="", flush=True)
sys.exit(7 if mode == "nonzero" else 0)
