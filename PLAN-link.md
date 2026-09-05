# The link under the session, second draft

*How a board and TikuDesktop talk, formalized so the window session
rides a UART console, a Bluetooth radio or an IP connection without the
session or the toolkit knowing which, on every TikuOS board.  This draft
amends the first after a critique of it against the failures actually
met.  2026-09-05.*

## 0. What the first draft got wrong

The first draft had the right seam -- a message link with a backend per
medium -- and four faults:

1. **It had no answer to the fault that caused every flaky run.**  The
   console has no flow control; the board drops bytes while busy, and a
   frame with no checksum then parses as garbage, the desktop's listener
   sees a nonsense length, drops the whole session, and the window
   vanishes.  Every "windows: 2" this week was that.  A transport plan
   that does not make the console link reliable moves the problem, not
   solves it.  Integrity, resynchronisation and pacing are now a
   milestone of their own, with fault injection to prove them.
2. **It put IP and BLE backends in the kernel.**  They depend on
   `tikukits/net` and `tikukits/net/bluetooth`, and the kernel must not
   reach into the kits (the shell's SLIP include is the one existing
   breach, not a precedent to widen).  The contract and the console
   backend belong in the kernel; the radio and IP backends are kits.
3. **Its console multiplexer chained per-byte sinks**, each carrying its
   own SLIP state machine -- which is exactly the duplication it set out
   to remove (`shell_net_demux` and `tiku_draw_frame_byte` are the same
   machine twice today).  The console should un-frame ONCE and dispatch
   whole frames by a channel marker.
4. **It did not say who reads the UART when the shell is not built**, or
   what a remote link may do.  Both are settled below.

## 1. The seam, unchanged in spirit

One layer between the session and the wire: a **link**, a bidirectional
pipe of whole messages.

```c
typedef struct tiku_link tiku_link_t;
int    tiku_link_send(tiku_link_t *l, const void *msg, size_t len);
size_t tiku_link_recv(tiku_link_t *l, void *buf, size_t max);
int    tiku_link_pump(tiku_link_t *l);
uint8_t tiku_link_cap(const tiku_link_t *l);     /* what it may touch */
```

Message-oriented, because each medium keeps two messages apart its own
way and a byte seam would make BLE, which is packet-oriented, pretend to
be a stream.  The contract promises: **a message arrives whole and
intact, or not at all** -- never a corrupted or partial one.  Each backend
earns that promise from its medium: TCP and BLE have it natively; the
console link must build it (section 3).

## 2. The console becomes a channelized line

**NEW `kernel/console/tiku_console.{h,c}`** owns the console backend that
today lives inside `kernel/shell/tiku_shell_io.c`, and adds one thing:
the line is SLIP-framed *channels*.

- Every `0xC0`-delimited frame on the line is un-escaped ONCE, here, and
  dispatched whole by its first byte, the channel marker:
  `0x4N` is an IPv4 packet (the SLIP channel, as the shell's demux
  already anchors it), `0xF1` is the window-session link, further
  markers are free for later channels.  Bytes outside any frame are the
  text channel: the shell's line editor.
- `tiku_console_add_channel(marker, handler, ctx)` registers a whole-
  frame handler; `tiku_console_send_frame(marker, bytes, len)` escapes
  and sends one.  `tiku_console_write(bytes, len)` is the text channel's
  raw TX.
- `tiku_console_pump()` drains the backend's RX and dispatches.  **When
  the shell is built it calls the pump from its loop, as it drains the
  UART today; when it is not, a small `console` protothread calls it.**
  The console therefore exists without the line editor, and the GUI runs
  on a shell-less build.
- The shell's `shell_net_demux` and the overlay's `tiku_draw_frame_byte`
  are both deleted; SLIP-IP becomes `add_channel(0x4N-match,
  ipv4_input)` and the GUI link `add_channel(0xF1, ...)`.  The
  `#if TIKU_APPL_GUI` hook in the shell's read loop goes with them.  One
  codec, one state machine, one place.

NUL escaping (`0xDB 0xDE`) is a property of the backend, set on consoles
that cannot carry a NUL (the MSP430 eZ-FET path); the nRF and USB-CDC
consoles carry it and do not escape it.

## 3. The console link, and making it reliable

**NEW `kernel/link/tiku_link.h`** -- the contract of section 1, a vtable
and nothing else, no dependencies -- and **`kernel/link/tiku_link_console.c`**,
a link on channel `0xF1` of the console.  This backend is where the
console's faults are answered, invisibly above:

- **Integrity.**  Every frame ends in a CRC-16 over the message; a frame
  that fails is dropped as a unit and counted, never parsed.  The
  listener never again sees a nonsense length from the console.
- **Resynchronisation.**  A dropped frame is survivable because the
  session already re-announces on a greeting: the desktop, on a counted
  loss or a silence past a deadline, re-greets, and the board announces
  its open windows again.  The window blinks rather than vanishes.
- **Pacing.**  The board's RX is the weak direction (it drops while it
  writes NVM or paints).  The desktop link paces its writes: a small
  window of unacknowledged frames, acknowledged by a one-byte channel
  frame from the board, so the desktop never floods the board.  Draws
  from the board to the desktop need no pacing; a PC does not drop.
- **Holding the burst.**  A board's window traffic waits for the
  greeting so it lands after the desktop's mount, as it does now; that
  stays, and is a consequence of the above rather than a special case.

## 4. The radio and IP backends, as kits

**NEW `tikukits/net/link/`** (a kit: it composes kits):

- `tiku_link_ip.c` -- over `tikukits/net/ipv4/tiku_kits_net_tcp`, which
  already offers `connect`/`listen` with `recv_cb`/`event_cb`; a u32
  length prefix per message; gated on `TIKU_KITS_NET_ENABLE`.  Dial-out
  from the board to a known desktop address first; mDNS if wanted later.
- `tiku_link_ble.c` -- over a GATT service (below); fragments a message
  across writes/notifies to the ATT MTU and reassembles; gated on the
  BLE driver macro.

**EXTEND `tikukits/net/bluetooth/tiku_bt.c`** with a service API it
lacks: `tiku_bt_gatt_add_service(service_uuid, write_uuid, notify_uuid,
on_write_cb)`, `tiku_bt_gatt_notify(conn, char, data, len)`, and the
service UUID in the advertisement.  A "TikuLink" service: one write
characteristic host-to-board, one notify characteristic board-to-host.

## 5. What a link may do

Each link carries a **capability**, as the console backend's `cap`
already restricts VFS writes.  The console link is `CAP_ALL` (physical
access).  A BLE link is restricted until the connection is paired (the
SMP pairing the BLE stack has); an IP link until it is inside TLS-PSK
(which the net stack has) or on a trusted address.  The forms road's
writes and the session's SAY/TELL inherit the link's capability, so a
window shown over the air cannot do more than its link is allowed to.

## 6. On the desktop

- NEW `kits/device/tiku_link.h` -- the desktop's mirror of the contract.
- NEW `tiku_link_serial.c` (the console channelization over `tiku_tx`,
  with the CRC, the acks, the pacing and the re-greet), `tiku_link_tcp.c`
  (a socket), `tiku_link_ble.c` (a BLE central via BlueZ, scanning the
  TikuLink service UUID).
- `tiku_channel.c` adopts a `tiku_link_t` rather than pairing a serial
  session with a socketpair; serial becomes one link of three.
- `tiku_session.c` loses its copy of the frame codec: framing is the
  serial link's.
- Discovery in `tiku_trk_desktop_mounts.c` grows from "scan serial" to
  "scan serial, scan BLE, try known IP", each yielding a link the remote
  listener adopts.

## 7. Milestones, each with its TikuBench proof

Every kernel change ships a firmware test module under the `TEST_*`
flags and a TikuBench suite that drives it over the port and reads the
`[TC:RUN]`/`[TC:END]` markers, in the manner of the existing `ble_*.py`
suites.

| # | Milestone | Where | Done when | TikuBench |
|---|---|---|---|---|
| **0** | The console channelized | `kernel/console/`; shell's demux and the overlay's decoder deleted; SLIP-IP and the GUI registered as channels | LM20 forms + drawing demos work exactly as today; `nm` shows one frame codec; `slip` networking still works | `console.py`: text and two channels interleaved on one line, every frame delivered whole to its handler, text intact to the shell; escape edge cases (`0xC0`/`0xDB` inside a frame) |
| **1** | The link contract and the console backend | `kernel/link/tiku_link.h`, `tiku_link_console.c`; `tiku_draw` writes to a link; desktop `tiku_link_serial.c`; `tiku_channel` adopts a link | `boarddraw.script` passes; no `tiku_shell_io` call in `board/gui/` | `link_console.py`: loopback of N messages in order, one of 4 KB, a burst of 50 |
| **2** | The console link made reliable | CRC-16, drop-as-a-unit, ack + pacing window, desktop re-greet on loss | `boarddraw.script` passes 20 runs of 20 back-to-back; the "windows: 2" failure is gone | `link_fault.py`: the suite corrupts, truncates and drops frames on purpose (a host-side fault injector in the serial link) and proves each is dropped whole, counted, and recovered by a re-greet; flood test proves the ack window holds |
| **3** | The board off the shell's back | the `console` protothread pumps when the shell is not built | a `TIKU_SHELL_ENABLE=0` build puts the board's window on the desk | `console_noshell.py`: the shell-less image answers a greeting with its window |
| **4** | The IP link | `tikukits/net/link/tiku_link_ip.c`; desktop `tiku_link_tcp.c` | the Pico W shows its window over WiFi, no cable; `boardnet.script` | `link_ip.py`: the host listens, the board dials, messages cross whole; reuses the net suite's WiFi bring-up |
| **5** | The BLE link | the GATT service API in `tiku_bt.c`; `tiku_link_ble.c` with MTU fragmentation; desktop BLE central | the LM20 shows its window over BLE, no cable; `boardble.script` | `link_ble.py`: extends `ble_gatt.py` -- the service advertises, a central subscribes, a 265-byte message crosses in three notifications and reassembles; a two-board variant as `ble_serial.py` does for boards without a host radio |
| **6** | Discovery and capability | the mount path finds serial, BLE and IP boards; each link's capability enforced on VFS writes and SAY/TELL | one desktop shows three boards over three media at once; an unpaired BLE link's write to `/gui` is refused | `link_cap.py`: a write over an unpaired link refused, the same write after pairing accepted |
| **7** | Portability proof | every board builds its backends behind its capability macros; the matrix documented | forms and drawing demos build for MSP430 (console), LM20 (console + BLE), Pico W (console + IP) and each reaches the desk over its best link | the three images built in CI; the board lanes above run per board |

Costs: 0 and 1 a day each; 2 two days (the acks and the fault injector
are the work); 3 half a day; 4 two days; 5 three days; 6 and 7 a day
each.  Ten to eleven days.

## 8. What stays, what goes

- Unchanged: the display-list op table and `check-toolkit`, the events,
  menus-as-text, the picker, the kit on the board.  The session is the
  session on every medium.
- Deleted: three copies of the frame codec, the `#if <transport>`
  branches in the shell's read loop, the serial assumption on the
  desktop.
- Decisions that are yours: message link over byte link (kept from the
  first draft); CRC-16 and a small ack window on the console rather than
  a heavier reliable-stream protocol; dial-out first for IP; GATT before
  L2CAP for BLE.
