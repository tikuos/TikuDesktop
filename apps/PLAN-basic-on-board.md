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

- **P0 — DONE 2026-09-08** (TikuBench `1500015`).  ~~pin the wire contract.~~  Firmware + bench, no desk.  The LM20
  USB-console image with `TIKU_SHELL_BASIC_ENABLE=1`, and a bench test
  (`basic-wire`, in the `basic` suite's home) that drives the mode over
  the console exactly as the desk will: enter, `NEW`, lines, `RUN`,
  output, the prompt's return as the end cue, Ctrl-C as the break, an
  `INPUT` fed and answered, `EXIT`.  Its PASS text names the prompt and
  the cues, so the IDE depends on facts a suite holds, not on a reading
  of the source.  Both DKs.  Half a day.
- **P1 — DONE 2026-09-08** (applications `c1095b4`).  ~~the lease.~~  Kit only, with `leaseboard.script` through the
  runner: lease, send `cat /sys/uptime`, the answer arrives on the
  callback, a namespace read meanwhile is refused and SAYS so, unlease,
  the same read succeeds.  Half a day.
- **P2 — DONE 2026-09-08** (applications `d8ddc71` + `d85a550`).  The runner is done and proven (applications
  `d8ddc71`): `kits/storage/tiku_board_basic.{h,c}` turns P0's cues into
  code on P1's lease -- hand it a program, pump it, take the output as
  lines, stop it, and the board is back at its shell afterwards.
  `test_boardbasic` proves it on a board (output, the two-line error the
  editor already folds, a stop, the namespace usable after); 8 runs clean.
  Two things it cost: the runner lives in the STORAGE kit, because it
  stands on the board BACKEND and the device kit may not draw on storage
  (the repo's own boundary check caught it); and a run must DRAIN what the
  last one left in flight, or that run's trailing prompt is taken for this
  one's and ends it before it starts.
  What is left of P2, and why it is the harder half: the editor is an
  APPLICATION, and the application ABI has no file or board service -- it
  reaches files only through host libc on paths the desktop's panel hands
  it, and a board lives in the desktop's namespace, not the filesystem.
  So "Run on..." needs a service seam: the desktop, which owns the mount
  and the lease, drives the runner and TELLS the session its output, the
  way the clipboard is told and `pick` is answered, because a service that
  returns data must never wait on the wire.  Then the message window
  consumes those lines exactly as it consumes the child pipe's today.
  ~~Run on a board.~~  The IDE: `board` line read and written,
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

## 6. What P0 and P1 settled (2026-09-08)

**P0 (TikuBench test 143, `basic-wire`).**  9/9 on an nRF54LM20B over its
own USB port and on an nRF54L15.  The cues P2 may now depend on: entering
says `Tiku BASIC ready` and prompts `ok> `; a program is entered a line at
a time; **the first `ok> ` after RUN arrives only once the program has
ended** -- that is the end cue, and it is withheld meanwhile; Ctrl-C on a
running program gives `^C break at line N` and leaves the REPL answering;
INPUT takes a line sent from off the board; BYE returns the shell.
Load-bearing: with the interpreter changed to prompt as soon as RUN starts,
the two prompt legs fail.  Measured while pinning it: the interpreter runs
about a thousand loop steps a second in the yielding mode, so a board
program is far slower than the same program on the host -- P2's message
window should not treat a quiet second as a finished run.

**P1 (kits `c1095b4`).**  `tiku_session_on_text` / `_send_raw` / `_drain`
and `tiku_backend_tiku_lease` / `_lease_send` / `_unlease` / `_leased`.
Proven on a pty and on a board, 15/15, five runs clean.  Two things the
building taught, both now in the code:

- **A refusal has to be immediate.**  Without the guard a leased backend's
  read still fails -- the holder's callback is taking the text, so the
  command never finds its prompt -- but only after its whole timeout.  The
  first version of the test passed on that accident; it now requires the
  refusal inside 250 ms, and the write to NAME the running program.
- **Handing the line back means draining it first.**  `tiku_session_sync`
  clears what it holds and takes the NEXT prompt it sees, so a prompt
  already on its way from the last client satisfied it while its own
  newline's answer was still coming, leaving the command after that one
  prompt out of step.  One run in four failed until unlease drained.

Also learned, and it shapes how P2 is tested rather than built: the
`basic` suite over `--console usb` dies at the first test that reboots the
board (125), because a native-USB console disappears and comes back as a
new device while a J-Link VCOM does not -- 185/203 on the LM20 against
290/292 on the L15.  A reboot-aware reconnect in the bench session would
close the whole class; until then, drive board-reboot suites over a VCOM.

## 7. What P2 settled, and the four things it cost (2026-09-08)

TikuBasic offers **Run on <board>** beside Run, runs the program there,
and what the board prints lands in the same message window a run on this
machine fills.  `iderunboard.script` proves it on a board found by
scanning with no port named: the board is offered BY NAME (which takes
the whole chain -- mount, shell service, wire, client cache, menu), and
running it opens the message window, which the editor only makes when
there is something to put in it.

The seam: the shell owns the mounts and the lease, so the SHELL runs the
program; an application asks by name and is TOLD the output, exactly as
the clipboard is told, because an ABI service that returns data must
never wait on the wire.  Four services, `board_said()` on the descriptor,
ABI 2, carried in process and over the wire both.

Four things the building cost, each now in the code:

1. **An application cannot reach a board at all.**  It gets files only
   through host libc on paths the shell's panel hands it, and a board is
   in the SHELL's namespace.  The seam is not an optimisation; without it
   there is no road.
2. **Names told once are told to nobody.**  The boards were named when
   they changed -- at startup, before any application had connected.  A
   session arriving later never heard them.  Told when the ROOM changes
   too.
3. **A run with no child must not be read as one.**  The editor's tick
   read the run's pipe; for a board run that descriptor is closed, the
   read returns EBADF, and the loop takes it for "a broken pipe is a
   finished run" -- the run ended before it had said anything.  Stopping
   one likewise asks the shell rather than killing a child that a board
   run has not got (with no child, the kill would signal this process's
   own group).
4. **A shared line must be resynchronised, and never waited on for ever.**
   The desk's own namespace reply was still in flight when the run took
   the line, so "basic" arrived appended to it and the board never
   reached the interpreter's prompt -- the run sat in SENDING and the
   shell sat with it.  A newline first, a bounded drain, and a bound on
   reaching the prompt at all: a run that cannot start FAILS rather than
   hanging the shell.

Not asserted in the script, and held elsewhere on purpose: what a message
row SAYS.  A remote window's drawing cannot be narrated over the wire and
the row menu offers a place rather than the words, so the two lines an
error is printed in -- and the fold that makes them one row with a line
-- are checked by `test_boardbasic` against a board instead.

