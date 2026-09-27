"""Fake test process: never imports hardware code or opens a device."""
import os
import sys
import time
import signal


def rec(*fields):
    print("\t".join(fields), flush=True)


def firmware(action, args):
    """`tikubench firmware ... --events tsv`, as records, for one fixed
    nRF54L15; it asserts what the desktop must never get wrong."""
    def opt(name):
        return args[args.index(name) + 1] if name in args else None
    sets = [args[i + 1] for i, a in enumerate(args) if a == "--set"]
    mode = os.getenv("TIKU_BENCH_FW_MODE", "normal")
    assert opt("--events") == "tsv"
    if action == "presets":
        assert opt("--mcu") == "nrf54l15" and "--board" not in args
        rec("presets", "board=nrf54l15", "console=uart")
        rec("presets:presets", "key=shell", "label=Shell",
            "flags=TIKU_SHELL_ENABLE=1 MCU=nrf54l15", "note=", "error=")
        rec("presets:presets", "key=kernel", "label=Kernel only, no shell",
            "flags=TIKU_SHELL_ENABLE=0 MCU=nrf54l15",
            "note=It has no shell", "error=")
        rec("presets:presets", "key=axon", "label=Shell with the Axon NPU",
            "flags=", "note=", "error=needs MCU=nrf54lm20b")
        return 0
    assert opt("--board") == "nrf54l15"
    preset = opt("--preset")
    if action == "options":
        # Asking what can be built never needs the board's port.
        assert "--port" not in args and not any(
            s.startswith("UART_BAUD") for s in sets)
        values = {"TIKU_SHELL_ENABLE": "1" if preset == "shell" else "0",
                  "TIKU_DRV_X_ENABLE": "0", "UART_BAUD": ""}
        for s in sets:
            k, _, v = s.partition("=")
            values[k] = v
        err = ("no X on this part" if mode == "refuse"
               and values["TIKU_DRV_X_ENABLE"] == "1" else "")
        rec("options", "board=nrf54l15", f"preset={preset}", f"error={err}")
        for name, kind, group, applies in (
                ("TIKU_SHELL_ENABLE", "bool", "System", "1"),
                ("TIKU_DRV_X_ENABLE", "bool", "Parts", "1"),
                ("TIKU_DRV_GPU_ENABLE", "bool", "Parts", "0"),
                ("UART_BAUD", "int", "Console", "1")):
            rec("options:options", f"name={name}",
                f"value={values.get(name, '0')}", f"kind={kind}", "choices=",
                f"group={group}", f"applies={applies}", f"help={name} help")
        return 1 if err else 0
    # A preview or a flash: the board has been handed over by now.
    assert opt("--port") == "/dev/fixture-only"
    rec("log", "text=SETS " + " ".join(sets))
    rec("stage", "stage=check", "state=start", "text=/dev/fixture-only")
    rec("status", "phase=before", "board=nrf54l15", "status.mcu=nRF54L15",
        "status.build=old 1", "status.boot_count=3", "status.store=ready")
    rec("stage", "stage=check", "state=ok", "text=nrf54l15 runs old 1")
    rec("stage", "stage=build", "state=start", "text=" + " ".join(sets))
    rec("log", "text=make: compiling")
    rec("image", "build=new 2", "board=nrf54l15dk", "cells=6")
    rec("stage", "stage=build", "state=ok", "text=image new 2")
    rec("preview", "known=1", "text=keeps name, alias", "note=")
    rec("note", "text=durable state: keeps name, alias")
    if action == "preview":
        rec("done", "ok=1", "action=preview", "board=nrf54l15")
        return 0
    assert action == "flash"
    rec("stage", "stage=flash", "state=start", "text=nrf54l15 via probe 1")
    if mode == "flashfail":
        rec("stage", "stage=flash", "state=fail", "text=Could not connect")
        rec("done", "ok=0", "action=flash", "board=nrf54l15")
        return 1
    rec("stage", "stage=flash", "state=ok", "text=new 2")
    rec("stage", "stage=verify", "state=start", "text=waiting")
    rec("status", "phase=after", "board=nrf54l15", "status.mcu=nRF54L15",
        "status.build=new 2", "status.boot_count=4", "status.store=ready")
    rec("stage", "stage=verify", "state=ok", "text=runs new 2")
    rec("done", "ok=1", "action=flash", "board=nrf54l15")
    return 0


if sys.argv[1] == "firmware":
    sys.exit(firmware(sys.argv[2], sys.argv[3:]))
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
if key == "host-only":
    assert not any(arg in sys.argv for arg in
                   ("--board", "--port", "--baud", "--flash", "--console",
                    "--make-var", "--extra-cflags"))
    cid = "host-check"
if "--only" in sys.argv:
    cid = sys.argv[sys.argv.index("--only") + 1].split(",")[0]
print(f"[TC:END] {cid} pass=3 fail=0", end="", flush=True)
sys.exit(7 if mode == "nonzero" else 0)
