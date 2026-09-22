#!/usr/bin/env python3
"""A board that goes quiet: a pseudo-terminal in front of a real port.

The desk is given the pty's slave as its port and everything crosses
to the real board and back -- until the timer runs out, when bytes
still cross INTO the board but nothing comes back, which is what an
unplugged-but-still-mounted board looks like from the desk's side.
After `--dead-for` seconds it answers again.

    deadport.py --port /dev/ttyACM4 --dead-after 8 --dead-for 20 \\
                --say /tmp/trk_deadport

writes the slave's path to the --say file once it exists, and runs
until killed.
"""

import argparse
import os
import select
import sys
import termios
import time
import tty


def raw(fd, baud):
    attrs = termios.tcgetattr(fd)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, "B%d" % baud, termios.B115200)
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dead-after", type=float, default=0.0,
                    help="seconds until the board stops answering; 0 never")
    ap.add_argument("--dead-for", type=float, default=1e9)
    ap.add_argument("--say", required=True,
                    help="file to write the pty path to")
    a = ap.parse_args()

    board = os.open(a.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    raw(board, a.baud)
    master, slave = os.openpty()
    tty.setraw(slave)
    path = os.ttyname(slave)
    with open(a.say, "w") as f:
        f.write(path + "\n")
    began = time.monotonic()
    dead_from = began + a.dead_after if a.dead_after > 0 else None
    dead_to = (dead_from + a.dead_for) if dead_from else None
    while True:
        now = time.monotonic()
        dead = dead_from is not None and dead_from <= now < dead_to
        r, _, _ = select.select([board, master], [], [], 0.05)
        for fd in r:
            try:
                data = os.read(fd, 4096)
            except OSError:
                data = b""
            if not data:
                continue
            if fd == master:
                os.write(board, data)            # into the board, always
            elif not dead:
                os.write(master, data)           # back to the desk, unless quiet


if __name__ == "__main__":
    main()
