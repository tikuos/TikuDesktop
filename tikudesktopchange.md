# TikuDesktop link refactor — status and handoff

*The transport seam under the board/desktop window session, so it rides a
UART console today and a Bluetooth radio or an IP connection later, on any
TikuOS board, without the session or the toolkit knowing which.*

**Milestones 0 to 3 are DONE and hardware-proven on the nRF54LM20A**
(0–1 on 2026-09-05, 2–3 on 2026-09-06), and **milestone 4's link is built
and proven** on the same board, with its desk leg and its radio leg still
open (§4c).  This document records what was built, where it lives, and how
it is proven, then hands the rest to the next agent.  The seven-milestone
arc and its rationale are in `PLAN-link.md` beside this file; read its
§0–§8 before carrying on.

---

## 1. Orientation: the repos, the build, the hardware

### Repo geography
- **`~/tikuOS`** — the firmware (the TikuOS kernel + arch).  Mainline.  A
  `-include experiment/experiment.mk` and, near the driver blocks, an
  `-include applications/applications.mk` pull in overlays.  Remote
  `github.com/tikuos/tikuOS`.
- **`~/tikuOS/applications`** — THIS directory.  A nested, independent git
  repo (`github.com/tikuos/tracker`).  Holds TikuDesktop and TikuTracker
  (the desktop side), the reusable `kits/`, the board-side GUI overlay under
  `board/`, and the plans.  Gitignored by the firmware repo; commit inside it.
- **`~/tikuOS/TikuBench`** — a nested repo, the over-the-port test harness:
  `python -m tikubench`, suites under `TikuBench/tikubench/suites/*.py`, the
  on-target C tests under `TikuBench/tests/`.  Commit inside it.
- **`~/tikuOS/tikukits`** — a nested repo, `tiku_kits_*` library code (net,
  BLE, sensors).  Commit inside it.

Each nested repo is committed in its own directory.  `experiment/` is a
private overlay and off-limits.  The three repos have their own
`commit-msg` hook (see §8); commit as `Ambuj Varshney
<ambuj.varshney@proton.me>` with **no trailers**.

### The overlay contract (do not break it)
`applications/applications.mk` is `-include`d by the firmware Makefile and
may only **add** `SRCS`, include paths and `-DTIKU_APPL_*` macros, and rely
on mainline hooks **guarded by those macros**.  It may NEVER patch a
mainline firmware source.  Invariant: with `applications/` absent the
firmware build is byte-identical to a tree that never had it — verified by
symbols, `nm main.elf | grep -c 'tiku_draw\|tiku_gui'` is 0 without the
overlay.  Any kernel change goes in `~/tikuOS/kernel/…` as legitimate
mainline; the overlay only *registers* against the new kernel API.  The
GUI's single mainline hook is `tiku_draw_init()`, called beside the `/gui`
attach in `tiku_vfs_tree_init()` (`kernel/vfs/tiku_vfs_tree.c`), inside
`#if TIKU_APPL_GUI`.

### Build / test / flash commands
Desktop side (run in `~/tikuOS/applications`):
```
make -j$(nproc)          # TikuDesktop, TikuTracker, kits, unit tests
make test                # unit tests + gates + the shell-script suite
make lint                # hygiene comment style + tools/check_style.py
./build/tests/test_console_x   # one unit test binary (e.g. test_link)
```
Firmware side (run in `~/tikuOS`), board = nRF54LM20A on the bench:
```
# The GUI image (a window on the board, its demos):
make MCU=nrf54lm20a TIKU_SHELL_ENABLE=1 TIKU_APPL_GUI=1 \
     TIKU_APPL_GUI_DEMO=1 TIKU_APPL_DRAW_DEMO=1 -j$(nproc)
make flash MCU=nrf54lm20a TIKU_SHELL_ENABLE=1 TIKU_APPL_GUI=1 \
     TIKU_APPL_GUI_DEMO=1 TIKU_APPL_DRAW_DEMO=1
```
`main.elf` is ONE shared file across MCUs; `rm -rf build/<mcu> main.elf`
before a cross-MCU or flag-changed rebuild or you ship stale objects.

### The board on the bench
- nRF54LM20A-DK (PCA10184), `MCU=nrf54lm20a`.
- Console is UARTE30, bridged by the on-board J-Link to **`/dev/ttyACM0`**,
  **115200 8N1, no flow control**.  `/dev/ttyACM1` is NOT a UART.
- Reflashing resets `static` state (the GUI's `greeted` flag persists across
  desktop sessions otherwise — see the flakiness note below).

### Harness rules learned the hard way
- NEVER run two harness runs, or a harness run and `make test`, at once:
  they share `/tmp/trk_*` homes and cause spurious cross-suite failures.
- Kill only `pkill -x TikuDesktop` / `pkill -x TikuTracker` (exact name),
  never `pkill -f`.
- Run one board draw script:
  `TRK_PORT=/dev/ttyACM0 TRK_SHELL_ONLY=boarddraw.script sh \
   TikuTracker/tests/shell/run.sh`
- **`boarddraw.script` now passes 20 runs of 20 back-to-back** (milestone 2).
  The old `windows: 2` symptom had two causes, both closed: a dropped console
  byte corrupting a frame (the transport, fixed by the CRC + ack window), and
  a test-helper bug where `click-window` landed on the board window's close
  box and shut it (the board never reboots between runs, so it stayed shut) —
  `tab_point` in `TikuTracker/src/shell/adapters/tiku_trk_script_adapter.c`
  now aims at the tab's middle, clear of the close and zoom boxes.  Reflash +
  `rm -rf /tmp/trk_home` for a clean first run; the loop reboots once and runs
  the script back to back.  A run script is in the scratchpad but the harness
  is just: `TRK_PORT=/dev/ttyACM0 TRK_SHELL_ONLY=boarddraw.script sh
  TikuTracker/tests/shell/run.sh` in a loop, with `pkill -x TikuDesktop`
  between runs.

---

## 2. What the seam is (built as designed)

One layer between the session and the wire, in two parts:

1. **The console (`kernel/console/`)** turns the line into SLIP-framed
   *channels*: one decoder un-escapes every `0xC0`-delimited frame ONCE and
   dispatches it whole by its first byte — the channel marker.  Bytes
   outside a frame are the text channel (the shell's line editor).
2. **The link (`kernel/link/`)** is a bidirectional pipe of WHOLE MESSAGES
   over any medium.  Its console backend rides one marked channel.  Message-
   oriented because each medium keeps two messages apart its own way.

Contract: **a message arrives whole and intact, or not at all** — never
corrupt or partial.  Milestones 0–1 built the seam and the console backend;
the integrity/pacing that *earns* the promise on a flow-controlless console
is milestone 2.

---

## 3. Milestone 0 — the console channelized  ✅ DONE

**One frame codec and one demux, in the kernel, dispatching whole frames by
marker.  SLIP-IP and the GUI are channels; `shell_net_demux` and
`tiku_draw_frame_byte` are deleted.  Behaviour on the wire is byte-identical
to before.**

### As built — `kernel/console/tiku_console.{h,c}`
The public API (read the header; it is the contract):
```c
/* The physical line: three calls + whether it can carry a NUL.  The boot
 * console (UART, or USB CDC on a native-USB build) is installed by default;
 * a test installs a RAM loopback in its place. */
typedef struct { void (*putc)(char); uint8_t (*rx_ready)(void);
                 int (*getc)(void); uint8_t escape_nul; } tiku_console_wire_t;
void tiku_console_set_wire(const tiku_console_wire_t *wire);

/* A channel: frames whose first byte == value under mask go to fn,
 * reassembled in the caller's own buf.  keep_first delivers the first byte
 * as payload (the IPv4 nibble) or strips it (a marker).  The registrant
 * brings the buffer, so the kernel needs no MTU/kit knowledge. */
typedef void (*tiku_console_frame_fn)(void *ctx, uint8_t *buf, size_t len);
int  tiku_console_add_channel(uint8_t value, uint8_t mask, uint8_t keep_first,
                              tiku_console_frame_fn fn, void *ctx,
                              uint8_t *buf, size_t cap);
void tiku_console_remove_channel(uint8_t value, uint8_t mask);

/* Out: one frame on a marked channel (END, marker, head+body escaped, END);
 * two parts so a payload already in a buffer needs no second copy. */
int  tiku_console_send_frame(uint8_t marker, const void *head, size_t hlen,
                             const void *body, size_t blen);
void tiku_console_write(const void *bytes, size_t len);   /* raw text TX */

/* In: the next TEXT byte, frames met on the way dispatched (the shell reads
 * here); or the pump for a caller that owns the console but not the keyboard
 * (a blocking builtin, or a shell-less build — milestone 3). */
int  tiku_console_getc(void);
void tiku_console_set_text_sink(tiku_console_text_fn fn, void *ctx);
void tiku_console_pump(void);

const tiku_console_stats_t *tiku_console_stats(void);  /* frames/stray/oversize/phantom */
```

### Decoder invariants (preserved from the old demux — DO NOT regress)
- `0xC0` (END) is a delimiter, never a parity toggle.  The byte AFTER an END
  selects the channel or is text; a stray or doubled END costs only itself.
  An END that closed a frame is NOT counted stray (it is the delimiter
  before the following text).
- Channel match is `(byte & mask) == value`: SLIP-IP is `(0x40, 0xF0, keep)`
  (the IPv4 version nibble), the GUI `(0xF1, 0xFF, strip)`.
- ESC decode: `0xDB` then `0xDC→0xC0`, `0xDD→0xDB`, `0xDE→0x00`.
- **30 s phantom guard** (`TIKU_CONSOLE_FRAME_TTL`): a frame open longer than
  30 s is abandoned and its bytes become text.  MUST stay 30 s — TLS crypto
  legitimately pauses the pump for seconds mid-frame; a 1 s guard sprayed
  live payload into the console.  The C test overrides it to 16 ticks so the
  guard proves in seconds.
- A frame that outgrows its channel buffer is dropped WHOLE and counted
  (`oversize`), never delivered truncated.

### The rewiring (all in `kernel/shell/`)
- `tiku_shell.c`: `shell_net_demux()` and the `#if TIKU_APPL_GUI` per-byte
  hook are DELETED.  The read loop drains through `shell_getc()`, which
  returns TCP bytes when a telnet client owns the line and otherwise
  `tiku_console_getc()`.  `tiku_shell_net_pump()`/`_getc()` (used by BASIC's
  blocking builtins) now delegate to `tiku_console_pump()`/`shell_getc()`.
- `commands/tiku_shell_cmd_slip.c`: `slip on` registers the IPv4 channel
  `(0x40,0xF0,keep)` with an MTU buffer and hands frames to
  `tiku_kits_net_ipv4_input()`; `slip off` removes it.  This keeps the one
  existing kernel→kit breach inside the SLIP command, not in the console.
- `commands/tiku_shell_cmd_console.{c,h}`: the `console` command prints the
  channel table and the stray/oversize/phantom counters.  (`console echo` is
  a milestone-1 addition — see §4.)
- `kernel/vfs/tiku_vfs_tree.c`: calls `tiku_draw_init()` beside `/gui`.

### The overlay — `board/gui/tiku_draw.c`
`tiku_draw_frame_byte()` DELETED.  `tiku_draw_init()` (RX) registers the
window-session channel; RX arrives as whole messages via `on_frame` →
`take_message()`.  (TX moved to the link in milestone 1.)

### Proof
- On-target `console` C category (`TikuBench/tests/kernel/console/`): 27/27.
  Run: `python3 -m tikubench run kernel --only console --board nrf54lm20a
  --flash --port /dev/ttyACM0`.
- Host suite `console.py` (`TikuBench/tikubench/suites/`): 9/9, interleaves a
  shell command with the window session's frames on one line.  Run:
  `python3 -m tikubench run console --board nrf54lm20a --flash --port
  /dev/ttyACM0`.
- Overlay byte-identity: `nm main.elf | grep -c 'tiku_draw\|tiku_gui'` = 0
  without the overlay; the frame codec exists once.
- Desktop `make test`, `make lint` green.

---

## 4. Milestone 1 — the link contract and the console backend  ✅ DONE

**`tiku_link_t` on both ends; `tiku_draw` writes to a link; the desktop's
channel adopts a link; the serial path is one link backend.  Behaviour
still matches (reliability is milestone 2).**

### As built — kernel
- `kernel/link/tiku_link.h` — the vtable, no dependencies beyond
  `stddef`/`stdint`.  `tiku_link_send(l,msg,len)`, `tiku_link_send2(l,head,
  hlen,body,blen)`, `tiku_link_on_recv(l,fn,ctx)`, `tiku_link_deliver()`
  (for a backend), `tiku_link_pump()`, `tiku_link_cap()`, `tiku_link_close()`.
  A `tiku_link_t` is `{const vtable*, void *ctx, uint8_t cap, recv, recv_ctx}`.
- `kernel/link/tiku_link_console.{h,c}` — a link on console channel `0xF1`
  (`TIKU_LINK_CONSOLE_SESSION`).  `tiku_link_console_open(&state, marker,
  buf, cap)` registers the channel and returns the link; `cap = CAP_ALL`
  (physical console).  send = `tiku_console_send_frame`; recv = the channel
  handler calls `tiku_link_deliver`.
- `kernel/link/*.c` are in the firmware `SRCS` unconditionally next to the
  console; `--gc-sections` drops them in a build with no link consumer, so
  overlay byte-identity holds.
- `commands/tiku_shell_cmd_console.c` gained `console echo on|off`: it arms
  an echo link on a spare channel `0xF2` that sends back every message it
  receives — a host loopback that exercises kernel/link end to end over the
  port without a desktop.

### As built — the board — `board/gui/tiku_draw.c`
`raw()/escaped()/send_begin()/send_msg()` and the `FR_*` codec are GONE.
`tiku_draw_init()` opens the console link and installs `on_frame` as its
receiver.  A message is one `tiku_link_send2(link, head, hlen, body, blen)`;
`present` streams the window's list as the body straight from the window's
own buffer after a 20-byte head (no second copy).  **No `tiku_shell_io` call
remains in `board/gui/`** (`grep -rn tiku_shell_io board/gui/` is empty).

### As built — the desktop — `kits/device/`
- `tiku_link.{h,c}` — the desktop's mirror of the contract:
  `tiku_link_send`, `tiku_link_on_message`, `tiku_link_deliver`,
  `tiku_link_close`.
- `tiku_link_serial.{h,c}` — the serial backend over a `tiku_session`: it
  routes the session's whole frames to a message callback and sends via
  `tiku_session_send_frame`.  It does NOT own the session.
- `tiku_channel.c` adopts a `tiku_link_t` (opened over the session with
  `tiku_link_serial_open`) instead of registering on the session directly;
  `tiku_channel_open/greet/fd/pump/close` signatures are unchanged, so
  `tiku_trk_desktop_mounts.c` and the discovery path are untouched.
- The desktop frame codec now lives in ONE place, `tiku_session.c`
  (`sort_byte` decode + `tiku_session_send_frame` encode); the link and the
  channel both go through it.

### Proof
- On-target `link` C category (`TikuBench/tests/kernel/link/`): 13/13.  Run:
  `python3 -m tikubench run kernel --only link --board nrf54lm20a --flash
  --port /dev/ttyACM0`.
- Host suite `link_console.py`: 6/6 (ordered N, one 256-byte = echo-buffer-
  sized, a burst of 50, the special bytes, no drops).  Run:
  `python3 -m tikubench run link-console --board nrf54lm20a --flash --port
  /dev/ttyACM0`.
- Desktop unit `TikuTracker/tests/test_link.c` over a pty: 6/6, both ways,
  one 4 KB message.  `test_channel.c` still 5/5, `test_gui.c` still green.
- `boarddraw.script` on a fresh LM20: 8/8.  Desktop `make test`, `make lint`
  green; `check-toolkit` holds.

---

## 4a. Milestone 2 — the console link made reliable  ✅ DONE

**A CRC-16 on every frame, a numbered and acknowledged stream from the
desktop to the board, a desktop that re-greets on a fault, and a fault
injector that proves each of them over the port.  The board never waits;
the desktop paces.**

### The frame inside a link's channel (both ends, and `frames.py`)

```
+0  ctl      bits 0-1 kind: 0 DATA, 1 ACK, 2 SYN;  bit 7 WANT_ACK;  rest 0
+1  seq      DATA/SYN: this frame's number (mod 256); ACK: the last delivered
+2  payload  DATA only
-2  crc      CRC-16/CCITT-FALSE (0x1021, init 0xFFFF) over ctl..payload, high
             byte first.  "123456789" -> 0x29B1
```
`TIKU_LINK_CONSOLE_OVERHEAD` = 4; a channel buffer holds the message plus
four.  The window session's channel takes `TIKU_LINK_CONSOLE_SESSION_MTU`
(256) bytes of message; the desktop refuses (`refused`) anything larger
before it is sent, so a long clipboard broadcast is dropped, not stalled.

### Receiver rules (identical in `tiku_link_console.c`, `tiku_link_serial.c`,
`frames.py`)
1. Shorter than 4 bytes, or a wrong CRC → dropped whole, `bad++`, nothing
   sent; nothing is ever delivered before the CRC is checked.
2. ACK → the sender's window bookkeeping; never delivered.
3. SYN → `paced_expect = seq+1`, the receiver's own outgoing numbering
   restarts at 0 (so the peer counts the stream from zero), ACK(seq).
4. DATA without WANT_ACK (the board's own frames) → delivered as it comes; a
   seq that is not expected counts one `gap` (once per frame).
5. DATA with WANT_ACK (the desktop's frames) → delivered once, in order, THEN
   ACK(seq); a seq up to `DUP_SPAN` (8) behind is a repeat whose ACK was lost
   (ACK again, `dup++`, not delivered); anything else is `reject`ed unanswered.

### Sender rules (the desktop's `tiku_link_serial.c`; the board never paces)
- WINDOW=2 in flight, PENDING=32 behind; a SYN opens the stream and re-opens
  it after any loss.  A cumulative ACK pops the window; RETRY_MS=500 without
  an ACK resends the whole window (go-back-N); RETRIES=4 rounds → a loss:
  everything in flight and pending is dropped, one fault is reported through
  `tiku_link_lost()`.  A link never acknowledged goes dead (no link there);
  one that was re-SYNs on its next send.

### The channel's greet policy (`tiku_channel.c`, link-agnostic)
A greet is the only WELCOME.  On a fault the socket backlog (stale events) is
flushed and the board re-greeted; on silence 2 s after a greet with nothing
received, re-greeted; two re-greets, then a 10 s heartbeat, so a board with a
link and no windows costs a few bytes a minute.  The socket is read only while
the link is ready.  The board's listener treats a repeated OPEN with the same
id as the same window, so a re-greet repaints rather than duplicates.

### Also done, off the seam
- `tiku_console_frame_begin/put/end` in `kernel/console/` — compose a frame
  from more parts than `send_frame` takes, no copy; `send_frame` wraps them.
- `tiku_session.c` on the desktop now drops an oversize frame whole (it used
  to deliver it truncated).
- `tab_point` (the script adapter's window-tab locator) aims at the tab's
  middle, so `click-window` cannot land on the close box (the back-to-back
  `windows: 2` closer).

### Proof
- On-target `link` C category (`TikuBench/tests/kernel/link/`): 46/46 (adds
  integrity and pacing groups to the contract + loopback).  Run: `python3 -m
  tikubench run kernel --only link --board nrf54lm20a --flash --port
  /dev/ttyACM0`.  On-target `console`: 27/27 (regression).
- Host `link-fault.py`: 10/10 — corrupt, truncate (short frame), drop (out of
  order), dup, recover (a desync healed by a SYN), a 100-message flood through
  the window, and a paced hold across a 1 s deaf board, each read against the
  board's own `link:` counters.  Run: `python3 -m tikubench run link-fault
  --board nrf54lm20a --port /dev/ttyACM0` (after a shell flash).
- Host `link-console.py`: 7/7; `console.py`: 9/9 (both ported to the link).
- `boarddraw.script` 20 runs of 20 back-to-back on the LM20: 20/20, the
  `windows: 2` failure gone.  The board's counters stay clean under load
  (`bad=0 dup=0 reject=0 gap=0`).
- Desktop `make test`, `make lint` green; firmware `make lint` clean on the
  link/console/shell files; overlay byte-identity holds (`nm` shows no
  `tiku_draw`/`tiku_gui` without the overlay).
- A native host harness for the kernel link (console + link + the C tests
  compiled with gcc against small stubs) is in the scratchpad, with mutation
  checks: breaking the CRC or the dup rule fails exactly the asserts that
  guard them.

---

## 4b. Milestone 3 — the board off the shell's back  ✅ DONE

**A `TIKU_SHELL_ENABLE=0` image puts the board's window on the desk.  The
kernel's console runs a process of its own that drains the wire while a
channel is registered; the desktop finds a board that answers no prompt on
its port alone and greets it.**

### As built — kernel (`kernel/console/tiku_console.{h,c}`)
- `TIKU_PROCESS(tiku_console_process, "Console")`, compiled only when
  `!TIKU_SHELL_ENABLE` (with a shell, the shell's loop reads the wire and
  the hooks below are no-ops).  It arms a one-shot timer of
  `TIKU_CONSOLE_POLL_TICKS` (`TIKU_CLOCK_SECOND / 20`, the shell's own
  cadence), and on each TIMER event re-arms it FROM NOW
  (`tiku_timer_restart`) and calls `tiku_console_pump()`, which on an
  RP2350 USB console is preceded by `tiku_usb_cdc_poll()` as in the shell.
  A POLL event drains at once (the hook for an RX-interrupt wake later).
- **Lazy start, self-ending.**  `tiku_console_add_channel()` and a non-NULL
  `tiku_console_set_text_sink()` start the process if it is not running;
  `remove_channel`, a NULL sink and `tiku_console_reset()` poll it, and its
  loop ends (timer stopped, `PT_END`) once nothing listens.  So an image
  that registers nothing — every kernel test category, the power images —
  runs no process and arms no timer, which keeps their idle measurements
  honest; `--gc-sections` drops the process from a build with no channel
  registrant at all (verified: the bare LM20 kernel has no
  `tiku_console_process`/`pump_timer` symbol).
- `tiku_console_pumping()` reports the process's state (always 0 with a
  shell); `tiku_console_process` is exported so a test can post to it.
- **Why restart, not reset.**  The first cut re-armed with the drift-free
  `tiku_timer_reset`, and the on-target test — which posts the TIMER by
  hand because the tests run with the tick stopped — wedged the board: a
  start advanced past a stalled clock reads as due at once under the
  wraparound test, so the timer process re-fires it, the pump re-arms it,
  for ever.  A pump wants drain-to-drain spacing anyway; `restart` is
  immune.  The shell keeps `reset` (its cadence also paces jobs and rules).

### As built — the desktop (`kits/device/`, `TikuTracker/src/shell/desktop/`)
- `tiku_channel_open_port(port, baud)`: a channel that owns its transport
  and session (`tiku_session_new` over `tiku_tx_open_serial`), for a board
  with windows and no shell.  `tiku_channel_pump()` polls that session
  itself (timeout 0) before tending the link, and reopens it once a second
  after a break (`tiku_session_reopen`, new: a transport reopen with no
  prompt sync, which `tiku_session_reconnect` now builds on).
  `tiku_channel_close()` frees the owned session.
- `tiku_channel_probe(ch, ms)`: pumps until the link's opening SYN is
  acknowledged (a board is there) or the link gives its window up (nothing
  there); a channel on a caller-owned session counts as answered.
- `mount_devices()` in `tiku_trk_desktop_mounts.c`: a port whose
  `tiku_backend_tiku_open()` fails (no prompt within 4 s) goes through
  `windows_only()`: open the channel on the port alone, probe it for up to
  3 s, keep it in a channel slot or close it.  `channels_attach()` then
  greets it like any other.  Nothing is mounted for such a board: the forms
  road is the shell's text and has no counterpart; the window comes up.
  The ~6 s a shell-less board costs at boot (the prompt wait, then the
  probe) is the price of not guessing.

### Proof
- On-target `console` C category: **37/37** (27 as before + the new
  `Console Pump` group, `TEST_CONSOLE_PUMP`: no pump while nothing is
  registered; the first channel starts it; its first turn arms the timer;
  a frame waits for the tick; a hand-posted TIMER drains it and re-arms; a
  POLL drains at once; one channel of two gone keeps it; the last gone ends
  it with the timer off; a text sink alone keeps it; its removal ends it).
  Run: `python3 -m tikubench run kernel --only console --board nrf54lm20a
  --flash --port /dev/ttyACM0`.
- Host `console-noshell` (`TikuBench/tikubench/suites/console_noshell.py`,
  firmware mode `noshell` = `TIKU_SHELL_ENABLE=0 HAS_TESTS=0 HAS_EXAMPLES=0`
  plus `TIKU_APPL_GUI=1 TIKU_APPL_DRAW_DEMO=1`): **7/7** — a newline draws
  no prompt and nothing arrives unasked; a SYN on `0xF1` is acknowledged; a
  WELCOME brings OPEN/DRAW/MENUS/SIZEABLE/DRAW back whole (host bad 0, gap
  0); the open names `Board window`; a press redraws `Pressed 1 time`; ten
  paced presses land (`Pressed 11 times`, 0 retransmits, 0 lost); typed
  lines between frames draw no answer and cost no frame.  Run: `python3 -m
  tikubench run console-noshell --board nrf54lm20a --flash --port
  /dev/ttyACM0`.  There is no `console` command on this image, so the host
  link's counters are the witness.
- Desktop unit `test_channel`: the new `a port of its own` scenario (6
  checks: a port that will not open; a channel with a session of its own;
  its SYN; a board that acknowledges is found; the pump polls the port
  itself and a message comes out whole; a silent port is given up on once
  the retries are spent, 2–4 s).  `make test`, `make lint` green.
- The desk: `TikuTracker/tests/shell/boardnoshell.script` (`# board: only`,
  needs the shell-less image on the cable): **3/3** — two windows, the
  board's beside the fixture's folder with nothing mounted; its title;
  a press on the demo's button (the same `click 421 390` as
  `boarddraw.script`, the window lands in the same place) narrates
  `Pressed 1 time`; screenshot at `/tmp/trk_boardnoshell.png`.  Run:
  `TRK_PORT=/dev/ttyACM0 TRK_SHELL_ONLY=boardnoshell.script sh
  TikuTracker/tests/shell/run.sh`.
  NOTE: the board lane of `run.sh` runs every `# board:` script against
  whatever image is flashed, so with the shell-less image on, the shell
  scripts (`board.script`, `boarddraw.script`) fail their mount, and with
  the shell image on, this one finds a mount and two extra windows.  Flash
  per script; the images are named in each script's header.
- Cross-target: the bare LM20 kernel (no overlay) links without the pump;
  the LM20 shell+GUI image builds with the hooks compiled to no-ops and no
  `tiku_console_process`; MSP430 FR5994 (large model) and RP2350 with the
  USB console (`tiku_usb_cdc_poll` path) compile.

---

## 4c. Milestone 4 — the IP link  ✅ the link, ⏳ the desk and the radio

**A board with an address dials a desktop and the window session rides the
connection.  A kit, not the kernel; a length in front of each message and
nothing else, since TCP delivers whole and in order.**

### As built — the kit (`tikukits/net/link/tiku_kits_net_link_ip.{h,c}`)
- `tiku_kits_net_link_ip_open(l, addr, port, buf, cap)` dials and returns a
  `tiku_link_t`.  `TIKU_KITS_NET_LINK_IP_PORT` is 7749.  Compiled by the
  firmware Makefile under `TIKU_KITS_NET_LINK_IP_ENABLE=1`, which errors
  without `TIKU_KIT_NET_ENABLE=1` under it.
- **The frame is a 32-bit little-endian length and the message.**  No CRC,
  no numbering, no acknowledgement: the console link earns those because
  its medium loses bytes, and TCP does not.
- **The outbox is the send contract.**  `ip_send` puts the WHOLE message
  (length, head, body) into `out[TIKU_KITS_NET_LINK_IP_OUTBOX]` (2 KB) or
  refuses it whole and counts `refused`; then offers the outbox to the
  transport in `snd_mss` chunks.  What the transport will not take waits
  for a flush timer (`stalled` counts that), because the first cut ABORTED
  the connection on a short write and the board RST every greeting: the
  TX pool holds a segment until it is acknowledged, so a burst of five
  announce messages meets a full pool routinely.  A length-framed reader
  cannot recover from half a message, and TCP keeps order, so waiting is
  the only correct answer.
- **What it sends and what it takes are different sizes.**  `cap` is the
  receive buffer (256, what a desk sends); the outbox bounds what the
  board sends (a window's display list, up to 1536).  The first cut used
  `cap` for both and refused every DRAW.
- Redials on its own five seconds after a connection ends, and steps its
  source port per dial, as MQTT does, so a board that resets does not reuse
  a port the peer still holds.
- **`tiku_kits_net_tcp_init()` is now idempotent** and the link calls it:
  its ONE caller was `tiku_shell_init` under `TIKU_SHELL_NET_TEST`, so in
  any other profile the segment pool was never created and every send was
  refused.  Same class as the MQTT-on-lean trap; the tier API's
  `init`-is-idempotent precedent is what this follows.

### As built — the board overlay (`applications/board/gui/`)
- `tiku_draw_set_link(link)`: the session moves to another link, holding
  its windows until that desktop greets (`said_hello`/`greeted` reset), as
  at boot.  `tiku_draw_init()` keeps the console link open underneath, so
  NULL returns the session to the cable.
- `tiku_draw_ip.{h,c}` + a reserved `desk` name in `/gui`'s dynops, both
  behind `TIKU_APPL_GUI_IP` (applications.mk): `write /gui/desk
  172.16.7.1:7749` dials, `write /gui/desk off` hangs up, and reading it
  gives the address, the state and the counters.  No new mainline hook --
  the namespace is the interface.

### As built — the desktop (`applications/kits/device/`, TikuTracker)
- `tiku_link_tcp.{h,c}`: a link over a socket a board dialled, with an out
  buffer for what the socket will not take now.  `TIKU_LINK_TCP_MTU` (256)
  bounds what it SENDS; `TIKU_LINK_TCP_RECV_MAX` (the session's 4096)
  what it takes.  `tiku_link_tcp_listen(port)` opens the listener.
- `tiku_channel_adopt(link)`: a channel on any link at all;
  `tiku_channel_open(session)` is now that over a serial link.
- `-listen [port]` on TikuDesktop (default 7749), OFF by default because a
  listening port is the desktop's own posture, not a session's assumption.
  `boards_accept()` takes in each dialled board, adopts it into the remote
  listener and greets it; `boards_unlisten()` at shutdown.

### Proof
- Host `link-ip` (`TikuBench/tikubench/suites/link_ip.py`): **8/8** on the
  LM20 — /gui/desk off; the board dials and the handshake finishes; the
  greeting brings OPEN/DRAW/MENUS/SIZEABLE/DRAW back whole; a press
  redraws; ten more land (`Pressed 11 times`); the board's own line reads
  `up dials 1 rx 12 tx 8 drops 0 refused 0 stalled 0`; `off` returns the
  session to the cable and closes the connection.  Run: `python3 -m
  tikubench run link-ip --board nrf54lm20a --flash --port /dev/ttyACM0`.
  **No root and no gateway**: `TikuBench/tikubench/core/tcp_peer.py` is the
  host's own end of the connection (SLIP wire + a one-connection TCP
  fixture), so the suite runs anywhere the board does.
- Desktop unit `test_link_tcp.c`: **18/18** over a loopback socket — whole,
  split and joined streams, one the size of the peer's buffer, one larger
  than any board sends passed over, the length-framed send, a refusal
  larger than the peer takes, the hang-up as a fault, and a channel
  adopting the link.  `make test`, `make lint` green.
- The kit's framing on the host: a harness in the scratchpad against a
  stubbed transport, 17/17, with mutation checks — delivering before a
  message is whole, dropping the oversize skip, or letting a stalled chunk
  through each fails exactly the asserts that guard them.
- **Not yet done: the desk leg.**  A real TikuDesktop showing a board's
  window over IP needs the board's packets routed to the host, which is
  the TUN gateway and therefore root: `sudo python3 -m tikubench ...` or a
  `boardnet.script` run under a gateway.  Everything under it is proven;
  what is unproven is `-listen` against a live board rather than a
  loopback socket.

---

## 5. File map (what changed, per repo)

```
~/tikuOS  (firmware)
  kernel/console/tiku_console.{h,c}          NEW  the one decoder/encoder
  kernel/link/tiku_link.{h,c}                NEW  the message contract
  kernel/link/tiku_link_console.{h,c}        NEW  the console backend
  kernel/shell/tiku_shell.c                  demux deleted -> shell_getc/console
  kernel/shell/tiku_shell.h                  net pump/getc docs
  kernel/shell/commands/tiku_shell_cmd_slip.c    IPv4 channel registration
  kernel/shell/commands/tiku_shell_cmd_console.{c,h}  NEW  console [echo] cmd
  kernel/shell/tiku_shell_config.h           TIKU_SHELL_CMD_CONSOLE flag
  kernel/vfs/tiku_vfs_tree.c                 tiku_draw_init() hook
  Makefile                                   console + link SRCS
  (CLAUDE.md has a "Console" section; it is gitignored in this checkout)

~/tikuOS/applications
  board/gui/tiku_draw.{c,h}                   codec gone; RX channel + TX link
  board/README.md                            "The wire" section updated
  kits/device/tiku_link.{h,c}                NEW  desktop contract
  kits/device/tiku_link_serial.{h,c}         NEW  serial backend over session
  kits/device/tiku_channel.{c,h}             adopts a link
  mk/kits.mk, mk/tracker.mk                   link sources + test_link link libs
  TikuTracker/tests/test_gui.c               driven through the link stub
  TikuTracker/tests/test_link.c              NEW  pty link test
  TikuTracker/tests/boardstub/kernel/link/*  NEW  link stubs for the host test

~/tikuOS/TikuBench
  tests/kernel/console/test_console.{c,h}    NEW  RAM-wire decoder tests
  tests/kernel/link/test_link.{c,h}          NEW  loopback link tests
  tests/{test_runner.c,tests.mk,tiku_test_config.h}  console + link categories
  tikubench/runner.py                        console + link category dicts
  tikubench/core/frames.py                   NEW  host frame build/split helper
  tikubench/suites/console.py                NEW  interleave suite
  tikubench/suites/link_console.py           NEW  loopback suite
```

Commits (by capability, one per repo per capability): firmware `Console:` +
`Link:`; applications `Board:` + `Desktop:`; TikuBench `Console:` + `Link:`.

Milestone 2 touched, per repo (one commit each):
```
~/tikuOS  (firmware) — `Link: CRC and an acknowledged stream on the console link`
  kernel/link/tiku_link_console.{h,c}        CRC, receiver rules, stats
  kernel/console/tiku_console.{h,c}          frame_begin/put/end (compose a frame)
  kernel/shell/commands/tiku_shell_cmd_console.{c,h}  the `link:` counters line
  TikuBench/tests/kernel/link/test_link.{c,h}, tiku_test_config.h,
    test_runner.c, tikubench/runner.py       integrity + pacing groups
~/tikuOS/applications — `Desktop:` (link) + `Board:`/adapter
  kits/device/tiku_link.{h,c}                ready/pump/lost/mtu ops
  kits/device/tiku_link_serial.{h,c}         paced sender, receiver, stats
  kits/device/tiku_channel.{c,h}             greet policy, flush, ready gate
  kits/device/tiku_session.c                 oversize frame dropped whole
  board/gui/tiku_draw.c, board/README.md     buffer = SESSION_MTU + overhead
  TikuTracker/src/shell/adapters/tiku_trk_script_adapter.c  tab_point clears close
  TikuTracker/tests/test_link.c, test_channel.c, boardstub/.../tiku_link_console.h
  TikuTracker/tests/shell/boarddraw.script   waits sized for the paced round-trip
~/tikuOS/TikuBench — `Link: A fault injector for the console link`
  tikubench/core/frames.py                   link_frame/link_parse, class Link
  tikubench/suites/link_fault.py             NEW  the fault injector
  tikubench/suites/link_console.py, console.py  ported to the link
```

Milestone 3 touched, per repo (one commit each):
```
~/tikuOS  (firmware) — `Console:` the pump without a shell
  kernel/console/tiku_console.{h,c}          the Console process, POLL_TICKS,
                                             pumping(), lazy start/self-end
  (CLAUDE.md "Console" section, gitignored here)
~/tikuOS/applications — `Desktop:` a board with windows and no shell
  kits/device/tiku_channel.{h,c}             open_port, probe, owned session
  kits/device/tiku_session.{h,c}             tiku_session_reopen
  TikuTracker/src/shell/desktop/tiku_trk_desktop_mounts.c  windows_only
  TikuTracker/tests/test_channel.c           the port scenario
  TikuTracker/tests/shell/boardnoshell.script  NEW  the desk proof
  board/README.md                            "The wire": the shell-less build
  tikudesktopchange.md                       this section
~/tikuOS/TikuBench — `Console:` the shell-less suite and the pump tests
  tests/kernel/console/test_console.{c,h}    Console Pump group
  tests/tiku_test_config.h, tests/test_runner.c  TEST_CONSOLE_PUMP
  tikubench/runner.py                        the flag in the console category
  tikubench/core/board.py                    firmware mode "noshell"
  tikubench/suites/console_noshell.py        NEW  the host suite
```

Milestone 4 touched, per repo (one commit each):
```
~/tikuOS  (firmware) — `Makefile:` the IP link kit's block
  Makefile                                   TIKU_KITS_NET_LINK_IP_ENABLE
  (CLAUDE.md's Link section, gitignored here)
~/tikuOS/tikukits — `Net:` the IP link
  net/link/tiku_kits_net_link_ip.{h,c}       NEW  the link over TCP
  net/ipv4/tiku_kits_net_tcp.{h,c}           init is idempotent
~/tikuOS/applications — `Board:` the desk + `Desktop:` the socket
  board/gui/tiku_draw.{h,c}                  tiku_draw_set_link
  board/gui/tiku_draw_ip.{h,c}               NEW  /gui/desk
  board/gui/tiku_gui.c                       the reserved desk name
  applications.mk                            TIKU_APPL_GUI_IP
  kits/device/tiku_link_tcp.{h,c}            NEW  the desktop's socket link
  kits/device/tiku_channel.{h,c}             tiku_channel_adopt
  TikuTracker/src/shell/desktop/*            -listen, boards_accept
  TikuTracker/tests/test_link_tcp.c          NEW  the loopback test
  mk/kits.mk, board/README.md, tikudesktopchange.md
~/tikuOS/TikuBench — `Link:` the IP suite
  tikubench/core/tcp_peer.py                 NEW  the host's end, no root
  tikubench/core/frames.py                   class Stream
  tikubench/suites/link_ip.py                NEW  the suite
```

---

## 6. What is LEFT — milestones 5–7

The seam is in place, reliable, stands without the shell, and carries a
session over an address as well as a cable; these build on it.  Full detail
and the TikuBench proof per milestone are in `PLAN-link.md` §7.  The
concrete starting points:

- **M4's remainder.**  A live desktop over IP (the `-listen` leg against a
  real board) needs the TUN gateway and root; and the Pico W over WiFi,
  where the same kit dials over a radio instead of SLIP, is the demo the
  plan asks for.
- **M5 — the BLE link.**  A GATT "TikuLink" service in
  `tikukits/net/bluetooth/tiku_bt.c` (add `tiku_bt_gatt_add_service` /
  `_notify`); `tiku_link_ble.c` fragments a message across notifies to the
  ATT MTU; desktop BLE central via BlueZ.  Proof: `link_ble.py` extending
  `ble_gatt.py`.
- **M6 — discovery and capability.**  The mount path finds serial, BLE and
  IP boards; each link's `cap` is enforced on VFS writes and SAY/TELL (an
  unpaired BLE link's write to `/gui` is refused).  The console link is
  already `CAP_ALL`; the seam for this is `tiku_link_cap()` +
  `tiku_vfs_caller_cap_set()`.  Proof: `link_cap.py`.
- **M7 — portability proof.**  Every board builds its backends behind its
  capability macros; forms + drawing demos reach the desk over each board's
  best link (MSP430 console, LM20 console+BLE, Pico W console+IP).

### Design decisions already made (keep them)
- Message link over byte link; the kernel owns the console + the contract,
  the kits own the radio/IP backends (the kernel must not reach into
  `tikukits/`).
- The console un-frames ONCE; consumers are registrants that bring their own
  buffer.
- CRC-16 + a small ack window on the console (M2), not a heavier reliable-
  stream protocol.  Dial-out first for IP.  GATT before L2CAP for BLE.

---

## 7. Hygiene and commit rules (enforced — do not fight the hook)

- A `commit-msg` hook (`hygiene/check_commit_msg.py`, wired through
  `hygiene/githooks` in EVERY repo here) REJECTS a message that is not
  `Area: what now works` with **≤5 one-line `-` bullets**, a subject over 72
  chars, a wrong author, or ANY trailer (Co-Authored-By / Generated with /
  Claude-Session).  Author is `Ambuj Varshney <ambuj.varshney@proton.me>`;
  plain `git commit`, no trailers, no `--no-verify`.  After a fresh clone run
  `hygiene/install_hooks.sh` once (`core.hooksPath` is per-repo local config).
- BEFORE committing, PRESENT the subject + author + body and WAIT for the go.
  One commit per capability (this work was six: Console/Link × firmware,
  Board/Desktop × applications, Console/Link × TikuBench).
- `make lint` AFTER staging (the comment-style checker skips untracked
  files).  `hygiene/` is gitignored; if absent, lint reports comment style
  UNCHECKED but the rules still apply (no milestone markers or debugging
  narrative in comments; a comment states the live hazard, not the route to
  it).  Note: a pre-existing VFS change-ring branch has an unrelated lint
  finding in `kernel/vfs/tiku_vfs.h` — not from this work.
- Desktop `tools/check_style.py`: functions ≤200 lines, ≤10 locals, headers
  ≤20 lines, 80 cols, every function documented.

---

## 8. Definition of done — status

- **Milestone 0 done:** one frame codec in the kernel; `shell_net_demux` and
  `tiku_draw_frame_byte` deleted; SLIP-IP and the GUI are channels; LM20
  forms + drawing demos and SLIP networking work as before; `test_console`
  27/27; `console.py` 9/9; overlay byte-identity holds; `make test`/`make
  lint` green.  ✅
- **Milestone 1 done:** `tiku_link_t` on both ends; `board/gui/tiku_draw.c`
  has no `tiku_shell_io` call and no `FR_*` codec; the desktop channel
  sources frames from a serial link; the codec exists once per side;
  `test_link` (firmware 13/13, desktop pty 6/6) and `link_console.py` 6/6
  pass; `boarddraw.script` 8/8 on a fresh LM20; `make test`/`make lint`
  green.  ✅
- **Milestone 2 done:** a CRC-16 and a numbered, acknowledged, paced stream
  on the console link; the desktop re-greets on a fault; an oversize frame is
  dropped whole; `tab_point` clears the close box.  On-target `link` 46/46
  and `console` 27/27; host `link-fault` 10/10, `link-console` 7/7,
  `console` 9/9; `boarddraw.script` 20/20 back-to-back on the LM20 with the
  `windows: 2` failure gone and clean board counters; `make test`/`make
  lint` green both sides; overlay byte-identity holds.  ✅
- **Milestone 3 done:** the console's own pump process in a shell-less
  build, started by the first channel and ended by the last; the desktop
  finds a board with no prompt on its port alone and greets it.  On-target
  `console` 37/37; host `console-noshell` 7/7; desktop `test_channel` with
  the port scenario, `make test`/`make lint` green; `boardnoshell.script`
  3/3 puts the shell-less board's window on the desk and presses its
  button from there; the bare kernel links
  without the pump and the shell image without the process.  ✅
- **Milestone 4, the link:** a board dials a desktop and the window
  session rides the connection, each message behind its length; the outbox
  holds what the transport will not take rather than tearing the stream; a
  desktop can listen for boards that dial in.  Host `link-ip` 8/8 (no
  root), desktop `test_link_tcp` 18/18, the kit's framing 17/17 on a host
  harness with mutation checks; `make test`/`make lint` green.  ✅
- **Milestone 4, what the plan still asks for:** a live TikuDesktop showing
  a board's window over IP (the `-listen` leg, which needs the TUN gateway
  and root) and the Pico W over WiFi with no cable, which is the same kit
  dialling over a radio.  ⏳
- **Out of scope here (milestone 5+):** the BLE/capability/discovery
  backends.  See `PLAN-link.md` §7.
