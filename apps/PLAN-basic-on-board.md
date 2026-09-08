# TikuBasic on a board — run it there, watch it here

> **Status: PLAN, not built.**  Written 2026-09-08 against TikuBasic as
> PLAN-ide.md left it (P1–P6 done: editors, project, messages, colouring,
> Build, the cold start) and against the boards on the bench that day: an
> nRF54LM20B on its own USB port and an nRF54L15 on a J-Link VCOM, both
> mounted by the desk's scan with no port named.

## 0. Context the executing agent must have

**What exists, and is the ground this stands on.**

- TikuBasic's Run today: the project's file goes to `tiku-basic`
  (`apps/basichost`), the board's interpreter compiled unchanged for the
  host, as a child on a pipe pumped from `tick()`.  Its output becomes
  messages `{text, file, line}` in the message window; an error is two
  lines, `? syntax` then `at line 250`, folded onto one row; a run is
  bounded because a program waiting on `INPUT` never ends on the host.
- A mounted board is `/devices/<name>`: `tiku_backend_tiku` over the
  window session's shell — `tiku_channel_shell_tx()`, `SHELL_IN` 21 /
  `SHELL_OUT` 22.  On the board, `applications/board/gui/tiku_draw_shell.c`
  injects the text into the console and steers each command's output to
  WHOEVER SPOKE LAST (a RAM `tiku_shell_io_t` in front of the cable's),
  in replies of at most 200 bytes.  The backend syncs on the shell prompt
  and bounds a write at 4 s.  An idle mount sends nothing of its own.
- The board's interpreter (`kernel/shell/basic/`): `basic` enters the REPL
  as a yielding shell MODE — the prompt returns, the program runs on the
  shell's ticks, keystrokes reach it through `tiku_basic_mode_feed_char()`,
  Ctrl-C is the break, `INPUT` reads through the same feed;
  `basic run` runs the saved program the same way, `basic run <path>`
  runs a `/data` file blocking, `basic load/save <path>` move a program
  to and from the store, and a checkpoint resumes it after a power cut.
  `TIKU_SHELL_BASIC_ENABLE=1` builds into the LM20's USB-console image
  (the memory says 1400 program lines fit); it is OFF by default on
  every non-Ambiq part, so the images the IDE lanes flash must ask.
- The name a board mounts under comes from its own identity name,
  sanitised, a clash taking a number (`device_mount_name`).  The project
  file already skips `board …` as a line the backend will need later.

**The one fact that shapes the transport.**  A program in the BASIC mode
OWNS the shell line: every byte the desk sends the board while it runs is
fed to the program as input.  So either the desk holds the board's shell
for the run and speaks nothing else, or the board gives BASIC a stream of
its own.  This plan does the first now and decides the second for later.

**Method (non-negotiable, as PLAN-ide.md):** proving script first,
mutation by name, full suite, both repos, one commit per capability.
A run on a board is proven with the board; a fixture proves a road
exists and also hides its absence.

## 1. The shape

Run gains a TARGET.  "Run" runs where the project says — `board <name>`
in `project.tiku`, or this machine when it says nothing — and the Run
menu carries "Run on…" listing this machine and every mounted board, the
choice remembered into the project.  Output lands in the SAME message
window under the SAME contract: it is the same interpreter, so a board's
error is the same two lines and folds onto the same row, and the caret
lands on the same line.  What is new in the window: an input line at the
bottom, live while a run is on, for a program's `INPUT`; and Stop, which
is Ctrl-C to the board.

Nothing is transferred as a file.  The program's lines go to the REPL as
`basichost` already feeds them, then `RUN`; a "Save on board" row does
`basic save /data/<name>.bas` afterwards, so the board keeps it and the
checkpoint road (`basic resume`) applies to what the desk sent.

## 2. The transport: the shell lease

Kit: `tiku_backend_tiku_lease(b, on_text, ctx)` takes the board's shell
for one client; while leased, `stat/list/read/write` answer EBUSY and the
status line says "running on <board>", `pump()` hands every byte the
board writes to `on_text` instead of the prompt sync, and
`tiku_backend_tiku_lease_send(b, bytes, n)` writes raw text with NO 4 s
bound and no prompt wait; `_unlease` returns the shell to the namespace
and re-syncs its prompt.  The run is: lease → `basic` → `NEW` → the lines
→ `RUN` → stream → the interpreter's prompt back (the end) → `EXIT` →
unlease.  Stop is Ctrl-C then the same tail.  The window's input line is
`lease_send` of the typed line.

Why a lease and not a second stream first: it needs no firmware change,
so the whole UI road — chooser, upload, streaming, errors, input, stop,
two boards — is proven end to end before any wire is touched; and the
cost it carries (the namespace is frozen while a program runs) is the
cost the shell already has on the cable today.

## 3. Phases, in order, each ending green and committed

- **P0 — pin the wire contract.**  Firmware + bench, no desk.  The LM20
  USB-console image with `TIKU_SHELL_BASIC_ENABLE=1`, and a bench test
  (`basic-wire`, in the `basic` suite's home) that drives the mode over
  the console exactly as the desk will: enter, `NEW`, lines, `RUN`,
  output, the prompt's return as the end cue, Ctrl-C as the break, an
  `INPUT` fed and answered, `EXIT`.  Its PASS text names the prompt and
  the cues, so the IDE depends on facts a suite holds, not on a reading
  of the source.  Both DKs.  Half a day.
- **P1 — the lease.**  Kit only, with `leaseboard.script` through the
  runner: lease, send `cat /sys/uptime`, the answer arrives on the
  callback, a namespace read meanwhile is refused and SAYS so, unlease,
  the same read succeeds.  Half a day.
- **P2 — Run on a board.**  The IDE: `board` line read and written,
  "Run on…" in the Run menu, upload through the REPL, streaming into the
  message window with the existing fold and caret, the end detected by
  the prompt, the child-pipe road untouched for this machine.
  `iderunboard.script` (`# board: only`): a hello program on the LM20
  over its USB port says its line; a program with a bad line puts the
  caret on it; Run while running says "already running".  One day.
- **P3 — interactive.**  The input line and Stop.  `iderunboardinput.
  script`: a program that `INPUT`s a name and prints it back, answered
  from the window; a program that loops forever, stopped, and the board's
  prompt back afterwards (the mount usable again — the proof that a
  stop is a stop).  "Save on board", proven by `basic load` on the
  console reading the file back.  Half a day.
- **P4 — two boards.**  "Run on…" lists both mounts; a project remembers
  its board; a board named but not mounted is SAID, with the list
  offered.  `iderunboards.script` under the discover lane, which finds
  both DKs with no port named: the same program run on each, each
  window saying its own board's `/sys/device/name`.  Half a day.
- **P5 — decided for later, not now: BASIC's own stream.**  Session
  kinds `BASIC_IN` 23 / `BASIC_OUT` 24, a board bridge beside
  `tiku_draw_shell.c` that feeds `tiku_basic_mode_feed_char()` directly
  and steers the interpreter's output to `BASIC_OUT` while a run is on.
  Then the namespace stays live during a run — the message window can
  show a `/data` file as the program writes it — and the lease shrinks
  to the upload.  Worth doing once P2–P4 have shown what a running
  program wants to see; not worth doing before.

## 4. What is deliberately NOT in this plan

No debugger, no breakpoints, no flashing a firmware from the IDE, no C
on a board (TikuC's Build stays the check it is), no Tier-3 module road,
no program editor on the board's window.  Each is a later argument.

## 5. Traps this session already paid for, so they are not paid again

- The board answers WHOEVER SPOKE LAST: the desk must send the `RUN`
  itself, or the program's output goes to the cable's UART.
- Replies are messages of at most 200 bytes and the outbox is bounded:
  a program printing fast can outrun the link; the window's partial-line
  buffer must hold, and the lease must never pace with the 4 s bound.
- A `write()` that returned is not a byte delivered; a suite that proves
  delivery uses the kernel's unsent count (TikuBench `usb-console`).
- Two J-Links on the rig: `TIKUBENCH_JLINK_SN` pins the probe; the desk
  scan matches the LM20 by the "TikuOS" in its by-id name.
- An abused board's control endpoint goes flaky; `nrfutil device
  recover` and a fresh flash is the baseline before blaming code.
