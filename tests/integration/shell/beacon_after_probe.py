# The beacon-after-a-link-session probe (docs/history/tikudesktopchange.md, "Soak and
# hygiene"): the LM20 on /dev/ttyACM0 with the BLE link image, the L15 peer
# (bleadv, console SEL=30) on /dev/ttyACM2.  Prints HEARD or SILENT per
# scenario; the second and later beacons after a session are the open case.
import serial, time
def opn(p):
    s = serial.Serial(p, 115200, timeout=0.1); s.write(b"\x03\n"); time.sleep(0.6); s.read(8192); return s
def cmd(s, line, secs=6):
    s.write(line.encode()+b"\n"); out=b""; t=time.time()
    while time.time()-t < secs:
        out += s.read(4096)
        if out.rstrip().endswith(b"tikuOS:/>"): break
    return out.decode("latin-1")
d = opn("/dev/ttyACM0"); p = opn("/dev/ttyACM2")
def desk():
    return [l.strip() for l in cmd(d, "cat /gui/desk").splitlines() if l.startswith("ble ") or l.strip()=="off"]
def beacon(tag):
    name = "TIKU-%s" % tag
    b = cmd(d, f"bleadv on {name} 100"); time.sleep(0.5)
    sc = cmd(p, f"bleadv scan 4 {name}", 12); cmd(d, "bleadv off")
    heard = [l.strip() for l in sc.splitlines() if name in l and "rssi=" in l]
    print(tag, "->", "HEARD" if heard else "SILENT", "| bleadv said beaconing:", "beaconing" in b, "| dbg:", [l.strip() for l in cmd(d, "bleadv dbg").splitlines() if l.startswith("RADIO") or l.startswith("BURST")])
cmd(d, "write /gui/desk off")
cmd(d, "write /gui/desk ble"); time.sleep(2); cmd(p, "bleadv central 6", 30)
print("right after the central:", desk()); time.sleep(2); print("2 s later:", desk())
for i in range(20):
    if any("free" in l for l in desk()): break
    time.sleep(1)
print("before off:", desk()); cmd(d, "write /gui/desk off"); print("after off:", desk())
beacon("B1")
cmd(d, "write /gui/desk ble"); time.sleep(2); cmd(p, "bleadv central 6", 30); time.sleep(1)
print("held?", desk()); cmd(d, "write /gui/desk off"); print("after off:", desk()); beacon("B2")
d.close(); p.close()
