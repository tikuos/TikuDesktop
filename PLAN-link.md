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
- `tiku_link_ble.c` -- over the byte pipe the radio already exposes
  (below); fragments a message to the pipe's maximum and reassembles.

**RIDE `interfaces/bluetooth/tiku_ble_serial.h`**, not a new GATT service.
The first draft of this section had the BLE backend add a "TikuLink"
service to `tikukits/net/bluetooth/tiku_bt.c`; that is the EM9305 host
stack (Apollo510 Blue), and on the Nordic parts the radio's own facade is
already a flow-controlled byte pipe over the Nordic UART Service layout --
start, ready (connected and subscribed), service, send, recv.  A link
backend over it is the same shape as the IP link's, and the facade is a
kernel interface, so the backend may live in `kernel/link/` beside the
console's without the kernel reaching into a kit.  Section 9 has the
milestone's plan as surveyed on the bench.

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
  with the CRC, the acks, the pacing and the re-greet) and
  `tiku_link_tcp.c` (a socket a board dialled, behind `-listen`).  BLE
  reaches the desktop through that same listener by way of a relay on the
  host (section 9), so no `tiku_link_ble.c` is needed here; a native
  BlueZ central is an optional follow-up.
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
| **5** | The BLE link (plan in §9) | `kernel/link/tiku_link_ble.c` over `tiku_ble_serial`, with fragmentation; a BLE relay on the desk, as IP's | the LM20 shows its window over BLE, no cable; `boardble.script` | `link_ble.py` on the D-Bus helpers of `ble_adv.py`: the board advertises, a central subscribes, a message larger than one notification reassembles at the negotiated MTU; a two-board variant as `ble_serial.py` does for boards without a host radio |
| **6** | Discovery and capability (DONE: `boardboth.script`, `link-cap` 6/6) | the mount path finds serial, BLE and IP boards; each link's capability enforced on VFS writes and SAY/TELL | one desktop shows three boards over three media at once; an unpaired BLE link's write to `/gui` is refused | `link_cap.py`: a write over an unpaired link refused, the same write after pairing accepted |
| **7** | Portability proof (builds DONE) | every board builds its backends behind its capability macros; the matrix documented | forms and drawing demos build for MSP430 (console), LM20 (console + BLE), Pico W (console + IP) and each reaches the desk over its best link | the three images built in CI; the board lanes above run per board |

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

## 9. Milestone 5, planned from the ground

Milestones 0 to 4 are built and proven (their record is in
`tikudesktopchange.md`).  A session of survey and bring-up on the bench
found where milestone 5 stands, and this is the plan that follows from it.
Everything above the link -- `tiku_draw_set_link`, the channel, the greet
policy, the desktop's listener -- is medium-agnostic already and needs
nothing; what the milestone has to earn is the radio.

### What the survey established

- The seam exists: `interfaces/bluetooth/tiku_ble_serial.h` (section 4).
  On Nordic it needs `TIKU_FLPR_ENABLE=1`, since the FLPR RISC-V
  coprocessor is the BLE controller; the toolchain is present and the
  build works.
- The host end exists: `hci0`, BlueZ, `python3-dbus`, and TikuBench already
  drives BlueZ over D-Bus (`suites/ble_adv.py`: `_discover`,
  `_reset_host_adapter`).  bleak's callbacks never fire for this device;
  use the D-Bus helpers.
- The radio works and this host hears its BEACONS: `ble-adv` decodes them
  over the air and BlueZ reports them within seconds.  A connectable
  advertiser is a different matter -- see below.
- The unknown, now traced to the end (2026-09-06).  `btmon` needs root
  here, so the board measured itself, and a SECOND radio measured the
  board: `bleadv scanreq` on the nRF54L15-DK is an active scanner that
  captures when a scan response arrives on a clock the advertiser has no
  hand in.  What it found, in order:
  - the coprocessor advertiser answered no SCAN_REQ at all; it now answers
    every one (316 of 316 in a run), with the service UUID rather than a
    copy of the advert;
  - the RADIO's `TIFS` does not govern the PHYEND_DISABLE + DISABLED_TXEN
    chain on this part: the reply left ~58 us after the request.  It is
    now fired by TIMER10 over DPPI (the compare on a channel opened only
    for a request that deserves an answer), and lands where the peer reads
    the spec's 150 us -- 200 ticks at 2 MHz, a figure the advertiser's own
    capture reads 40 ticks higher because its receive and transmit events
    fire at different points in a packet;
  - the DPPI channels carry their own SPU security attribute, so a
    non-secure radio and timer met on nothing until the channels were
    granted with the peripherals;
  - a received packet's buffer is ordinary memory the DMA writes behind the
    compiler's back: a header loaded for one packet was reused for the
    next.  Every parse now sits behind a barrier -- the coprocessor's
    "DMA settle" spin was this bug wearing a different name;
  - advertising events ran 8 ms apart, under the spec's 20 ms floor and
    with under a millisecond of jitter; they now run ~50 ms apart with the
    spec's 0..10 ms delay.
- What the host does with it: this MacBook's Broadcom BCM4364 on the T2
  UART runs without its firmware patch (`brcm/BCM.hcd` not found, per the
  kernel log) and its kernel logs advertising reports it drops, legacy
  ADV_IND types among them, at a steady rate with the board silent.  It
  reports the board's advert in about one scan session of three, the
  board's non-connectable beacon in about the same, and a 150 s search with
  the adapter reset between sessions found nothing in two of three runs.
  A Nordic central connects in under two seconds every time.  The board is
  right; this host's radio is the lottery, and no unprivileged user can
  change that.
- The trap, now diagnosed: `ble-adv` is 5/11 on the LM20 and the later
  tests report a hard fault -- but `addr2line` on the suite's own image
  puts the faulting PC in `scan_cb` / `tiku_radio_arch_scan_service`
  (`interfaces/bluetooth/tiku_ble_adv.c`, `arch/nordic/tiku_radio_arch.c`),
  NOT in `csa1`/`ackfsm`.  Those two are pure array logic; they only PRINT
  the already-parked banner.  The lead: `scan_cb` trusts the on-air LENGTH
  byte (`p->buf[1]`, up to 255) and parses the name at `buf[9]` for
  `len - 6` bytes out of a 48-byte ring packet (`radio_scan_pkt.buf[48]`),
  and the ISR `memcpy(..., 3u + n)` into that same 48-byte buffer can
  overrun when a DLE / combined session raises MAXLEN.  A bounds clamp
  there is Phase 0's likely fix.  Until it is fixed, reflash before
  believing any negative BLE result.

### Phase 0 -- make the bench trustworthy  DONE 2026-09-06

The fault was in the scan drain, not `csa1`/`ackfsm` (those are pure array
logic and only printed the already-parked banner).  Disassembling the
faulting PC put it on the slot store `ctx->out[count]` with a null base --
a precise bus fault at address 0, escalated to a hard fault that parked the
board.  Fixed in `BLE: Fixes for the scan drain parking the board`:

- `scan_cb` drops a report whose table is absent instead of storing
  through a null base.
- A ring entry's stored LENGTH is now what the entry HOLDS: the ISR
  clamped its copy to 44 bytes but kept the on-air byte, which can claim
  255, and the AD walk at `buf[9]` ran to LENGTH-6, past the 48-byte entry.
- The name walk is bounded by the largest legacy advertising payload.

`ble-adv` on the LM20 goes **5/13 with 24 fault lines to 10/13 with none**,
and four tests that only ever saw a parked board now pass (the observer
hears advertisers, the hardware window, the extended events, the LL
groundwork).  `link-ip` still 8/8.

Then the rest of it, and the null store's own cause, in `Timers: The
clock's width no longer depends on include order`.  `tiku_clock.h` fell
back to a 16-bit time when a translation unit reached it before `tiku.h`
(an include guard locks the first typedef in), so `struct tiku_timer` was
**24 bytes in `tiku_ble_adv.c` and 28 in the timer subsystem**: every timer
that file declared was four bytes short, and the timer's own stores landed
past the end of it.  On this board those four bytes fell on the object
following `observe_timer` in .bss -- the observer's report table -- so it
went NULL and the drain faulted through it.  Adding any .bss to that file
moved the damage elsewhere, which is why it read as a Heisenbug.  The
header decides the width itself now, by the same platform test, so the
type is identical whatever the include order; 44 files include it.

`ble-adv` on the LM20 is **13/13 with no faults**, from 5/13 with 24 fault
lines.  Regressions after the header change: `link-ip` 9/9, `console`
10/10, `console-noshell` 7/7, on-target `console` 37/37, and MSP430 (the
16-bit platform), Apollo4L and RP2350 all build.

The bench is trustworthy: a negative BLE result is now an honest one.

### Phase 1 -- the radio interop  ✅ on the board, ⏳ on this host

Answered (see "the unknown" above): the board advertises, answers scan
requests at the spec's mark and holds a link, as a second radio proves
end to end.  The instruments stay in the tree: `bleadv connprobe <s>
[tifs] [txen] [nonconn|scan]` on the advertiser, `bleadv scanreq <name>
[s] [txen]` on a peer, `bleadv flprnus [txen]` for the coprocessor.

- Exit as written: a host probe passes end to end.  DECISION taken: this
  host's radio does not qualify as the probe, so the two-board route is
  the milestone's, and `link_ble.py` stands ready for a host whose adapter
  reports what it hears.  It searches in fresh scan sessions with the
  adapter reset between them, for 150 s, before it gives up.

### Phase 2 -- the link (one day)

`kernel/link/tiku_link_ble.c` over the facade, the IP link's contract
again: a 32-bit length in front, a message queued whole or refused whole,
an outbox that waits for the pipe rather than tearing the stream, and the
size a board sends kept apart from the size it takes.  One thing is new:
the pipe must be serviced and no process on the board does that today, so
the link gets a timer process on the console pump's pattern -- started by
the first open, ended by the last close, so an image with no BLE link arms
no timer.  Prove the framing on a host harness against a stubbed facade,
with mutation checks, before it meets the radio.

`/gui/desk` gains one word: `write /gui/desk ble` starts advertising and
moves the session, `off` returns it to the console.

### Phase 3 -- the suite (half a day)

`link_ble.py`, on the D-Bus helpers `ble_adv.py` already has: the board
writes `ble` to its desk; the host discovers, connects, subscribes, and
speaks the session as length-framed bytes over UART Service writes; the
greeting brings the window back whole; a message larger than one
notification reassembles -- read the negotiated ATT MTU rather than assume
how many fragments that takes; presses redraw; `off` hangs up.  No root:
BlueZ's connect, subscribe and write are unprivileged.

### Phase 4 -- the desk (one day)

Not a BLE central inside TikuDesktop.  Milestone 4's listener already takes
any connection; a relay on the host, `ble_peer --to 127.0.0.1:7749`,
carries the BLE connection to it exactly as `tcp_peer` carries the IP one.
The desktop then shows a board's window over BLE with no new code,
`boardble.script` is `boardnet.script` with the other relay, and a native
central becomes an optional follow-up rather than a prerequisite.

### Done when

- `link_ble.py` green on the LM20 with BlueZ as the central, as a normal
  user.
- `boardble.script` puts the board's window on the desk over BLE and a
  press crosses.
- The framing harness passes and its mutations fail.
- `ble-adv` no longer parks the board.
- `tikudesktopchange.md` §4d records it done; the board README's wire
  section gains the BLE paragraph.

Three to four days, of which Phase 1 carries all the risk.  The first act
of the next session is the `btmon` trace.

## 10. Milestone 6, done

Discovery was mostly there: a serial scan when no port is named, a listener
for boards that dial, the BLE relay into that listener.  What was missing
was the two together on one desk, which run.sh's `both` lane runs and
`boardboth.script` proves with the two DKs on the bench (windows 3).

Capability over the air follows the central's standing.  The BLE serial
facade now answers a central's pairing, stores the key, and services the
encryption that follows; the BLE link confers `CAP_HW` while the link runs
encrypted under an agreed or recalled key and nothing otherwise; the
session reads its link's capability live.  `link-cap` 6/6, two-board.  The
desk line names the standing, so a failure names its step.

## 11. Milestone 7, the build half

Every supported board builds the window session behind its capability
macros (2026-09-06).  What is left of milestone 7 is the hardware half:
each board reaching a desk over its best link, which needs the boards.

| board | link | session symbols | IP link |
|---|---|---|---|
| msp430fr5994 (large model) | console | 29 | -- |
| nrf54lm20a | console | 29 | -- |
| nrf54lm20a | console + IP | 32 | 3 |
| nrf54lm20a, no overlay | -- | **0** | 0 |
| rp2350 | console + IP | 32 | 3 |
| apollo4l | console + IP | 32 | 3 |
| apollo510 | console + IP | 32 | 3 |
| ra8p1 | console + IP | 32 | 3 |

The no-overlay row is the overlay contract: a build without
`applications/` carries none of the session's symbols.

The RA8P1 needed `Net: Support for the RA8P1 in the stack's arch branches`
in `tikukits`: the slip, ipv4, dns and http files each pick an arch header
per platform and had no RA8P1 arm, so asking for the net stack there did
not compile.  That is a net-kit gap the IP link surfaced, not a link one;
the console road built there already.  The other four platforms build the
net stack as before.

Proven on hardware so far, all on the nRF54LM20A: the console link
(milestones 0-3), the IP link (milestone 4), and a link's capability
(milestone 6's half).  A second board of each kind is what the rest of
this milestone needs.
