# TikuDesktop link refactor — status and handoff

*The transport seam under the board/desktop window session, so it rides a
UART console today and a Bluetooth radio or an IP connection later, on any
TikuOS board, without the session or the toolkit knowing which.*

**Milestones 0 and 1 are DONE and hardware-proven on the nRF54LM20A**
(2026-09-05).  This document records what was built, where it lives, and how
it is proven, then hands the remaining milestones (2–7) to the next agent.
The seven-milestone arc and its rationale are in `PLAN-link.md` beside this
file; read its §0–§8 before starting milestone 2.

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
- **The board window is flaky BACK-TO-BACK** because its `greeted` flag and
  the desktop's `/tmp/trk_home` persist; the symptom is `windows: 2` (the
  board window never appears).  This is NOT a transport bug in milestones
  0–1 — it is the stale-greeting state.  Reflash + `rm -rf /tmp/trk_home`
  for a clean single run; the suite's contract is one run against a fresh
  board, which passes 8/8.  (Milestone 2's reliability work is a separate
  concern: a dropped byte under load corrupting a frame.)

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

---

## 6. What is LEFT — milestones 2–7

The seam is in place; these build on it.  Full detail and the TikuBench proof
per milestone are in `PLAN-link.md` §7.  The concrete starting points:

- **M2 — the console link made reliable** (the one that matters: it kills the
  `windows: 2` drop-a-byte flakiness).  Add to `tiku_link_console.c` /
  `tiku_link_serial.c`: a CRC-16 per frame (drop-as-a-unit on mismatch, count
  it), an ack + a small pacing window (the board's RX is the weak direction —
  it drops while it writes NVM or paints), and a desktop re-greet on a counted
  loss or a silence past a deadline so the window blinks rather than vanishes.
  Proof: `link_fault.py` — a host-side fault injector in the serial link that
  corrupts, truncates and drops frames and proves each is dropped whole,
  counted and recovered; a flood test proves the ack window holds.  Reuse
  `tikubench/core/frames.py` for the fault injector.
- **M3 — the board off the shell's back.**  A small `console` protothread
  calls `tiku_console_pump()` when the shell is not built, so a
  `TIKU_SHELL_ENABLE=0` image still puts a window on the desk.  Proof:
  `console_noshell.py`.
- **M4 — the IP link.**  `tikukits/net/link/tiku_link_ip.c` over the TCP
  stack (a u32 length prefix per message; dial-out from the board first);
  desktop `tiku_link_tcp.c`.  A kit, not the kernel.  Proof: `link_ip.py`,
  Pico W shows its window over WiFi.
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
- **Out of scope here (milestone 2+):** the CRC-16, the ack/pacing window,
  the desktop re-greet on loss, the shell-less `console` protothread, and the
  IP/BLE backends.  See `PLAN-link.md` §7.
