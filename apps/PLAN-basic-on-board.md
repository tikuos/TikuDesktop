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
- **P3 — DONE 2026-09-08** (applications `b210547` + `fc2b143`).
  script`: a program that `INPUT`s a name and prints it back, answered
  from the window; a program that loops forever, stopped, and the board's
  prompt back afterwards (the mount usable again — the proof that a
  stop is a stop).  "Save on board", proven by `basic load` on the
  console reading the file back.  Half a day.
- **P4 — DONE 2026-09-08** (applications `45ee9bb`).

- **P5 — DONE 2026-09-08** (tikuOS `e5d2408`, applications `fb261e2`).


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

## 8. What P3 settled (2026-09-08)

The editor now drives a program on a board the way it drives one here.

- **Stop** is offered on the project window the run was started from --
  not only on an editor page, which a run started from the project never
  opened.  It asks the shell to break the program rather than killing a
  child a board run has not got.
- **INPUT** is answered from the message window.  When the board's
  interpreter stops at "? ", the run notices, keeps the prompt, and the
  message window's bottom strip becomes a field with that prompt beside
  it; the line typed there goes to the program and it carries on.  A new
  ABI kind (ASKS) and a board_save service carry this in process and over
  the wire both.
- **Save on <board>** keeps the file in the interpreter's own named slot;
  the board shows it in its namespace as /data/<name>.bas, which is where
  the script reads it back through the shell's mount.

Four things the building cost, each fixed in the code:

1. **A command number collided with a base.**  The editor treats any
   command at or above CMD_WINDOW as "raise window N"; the new Run/Save
   numbers, assigned as max+1, landed there and returned before the
   switch, so the run never started and left NO trace on the wire.  The
   lesson that cost the most: the desktop's own log showed only the
   file-picker ask from Open Project, and the run produced nothing --
   the IDE runs remote, its stderr had to be proven captured before its
   own prints could be trusted.
2. **The last run's lines were still there.**  The interpreter keeps a
   program by line number; without NEW, the previous program's lines ran
   under the new one's.  NEW before the first line.
3. **A file made after mounting could not be read.**  The manifest is
   read once at connect and never lists a slot saved later; a read of an
   unlisted name now asks its directory first, the same shape the forms
   under gui/ are completed with.
4. **A shell-only board was fed frames as text.**  The desk greeted a
   window channel on every board; one without the overlay takes a frame
   for an unregistered channel as text, so every greeting landed on its
   shell line as junk.  The channel is opened only on a board that has
   a /gui.

Held at the kit, not the script: what a message row SAYS.  A remote
window cannot be narrated over the wire, so test_boardbasic checks the
INPUT prompt, the saved file's contents in the board's namespace, and
that a stopped program's rest never runs -- ten legs against a board.

## 9. What P4 settled (2026-09-08)

Two DKs on one desk -- an nRF54LM20B on its own USB console and an
nRF54L15 on a J-Link port -- both calling themselves `tiku`, so the
shell names the second `tiku-2`.  That pair of names is what the editor
offers: one **Run on <name>** row per mounted board, so which board a
program goes to is a choice rather than whichever answered first.  The
project REMEMBERS the board it was run on, in a `board` line of its own
description, and Save follows it.  A project naming a board that is not
on this desk says so, in a row of its own beside the boards that are --
because which board a program went to is not a detail to guess at.

`iderunboards.script`, 11 checks on the two real DKs: the same program
sent to each board in turn puts both lamps out, then one program sent to
one board lights that board's lamp and leaves the other dark.  The lamp
is the evidence, read back through each board's own namespace, and the
dark state it starts from is MADE by the feature rather than assumed.

Two things worth keeping:

1. **A command number is not a free integer.**  The editor reads any
   command at or above `CMD_WINDOW` as "raise window N" and returns
   before the switch; board commands assigned as max+1 landed there and
   the menu row did nothing AT ALL -- no run, and nothing on the wire to
   show for it.  They are now a declared range below that base, with a
   `_Static_assert` holding it, because the failure is silent.
2. **A fixture's file names are geometry.**  The Open panel lists the
   home folder, so renaming a fixture's programs moves the row the other
   scripts click by coordinate.  Two programs, the same two names every
   other editor fixture uses.

## 10. What P5 settled (2026-09-08)

The interpreter has a stream of its own on the link -- `BASIC_IN` 23 and
`BASIC_OUT` 24 -- so a run no longer takes the board's shell.  The
program, the RUN and the answers to INPUT arrive as messages; what the
program prints goes back the same way; and the console stays the
shell's throughout.  `iderunboardlive.script` reads
`/devices/tiku/sys/device/name` WHILE a program is running, with Stop
offered beside it to show the run really is in progress -- a reading no
board held by a lease could answer.  The lease did not shrink to the
upload: it is gone entirely for a board that offers a stream, and kept
whole for one that does not.

The kernel's part is small and off by default: a stream set on the mode
carries what the interpreter reads and writes, swapped in around the
mode's own hooks so a command's output and a program's never mix, and
while it is set the shell keeps its console line.  With no stream set
nothing changes, and with the overlay absent the image has none of it
(checked by symbols, not by size).

Four things the building cost, each a rule the code now keeps:

1. **A question must leave before its answer can arrive.**  INPUT prints
   its prompt with no line end and then waits in a loop of its own;
   nothing else was sending, so the desk waited for a question that had
   not gone out and the board for its answer.  The wait now flushes
   first and SERVES the port, rather than merely reading it.
2. **A standing prompt needs a beat of its own.**  The interpreter ends
   a program by printing `ok> ` -- again with no line end -- so a run
   that ended in an error was one the desk waited on for ever.  The
   bridge flushes on the shell's pump, exactly as the shell's own
   bridge does.
3. **Do not feed the interpreter from inside itself.**  The answer
   arrives while the board is already inside INPUT's read; fed there it
   would be read twice.  It is buffered, and the wait takes it.
4. **The mode's own state decides, not a flag beside it.**  A program
   that ended with BYE left the interpreter, and a second run fed to a
   mode that had exited went nowhere.

Not attempted here: the message window showing a `/data` file as a
program writes it.  The road to it is open now -- the namespace answers
mid-run -- but nothing yet asks.

## 11. Connect: where a program goes is chosen once (2026-09-09)

The Run-on-<board> rows were the wrong shape, and it was the user who
said so: one row per board per action sprawls, and a separate "look for
boards" verb is a chore nobody asked for.  What replaced it is the shape
every IDE with targets has.

**Connect** is a menu of its own on both the project window and the
editor page: **This machine**, then every board the shell has mounted,
the destination marked.  Picking one makes it the destination and the
project remembers it (its `board` line); picking This machine forgets.
**Run** is a single row that says where it will go -- "Run blink.bas on
tiku" -- and Save on <board> appears only when a board is the
destination.  A board the project names that is not here is said in
Connect, in a row that cannot be picked.

The shell looks for boards **when a project opens**: a deliberate act,
and a Connect menu that already lists what is here is worth the ports
being opened for it.  "Look again" sits at the bottom of Connect for a
board plugged in since; it is not a separate verb.  A port the shell
already holds is left alone, so looking twice mounts nothing twice --
which is the bug that hung the desktop when looking was a deskbar row.

Not in the shell at large.  A desktop-wide "Find boards" was built and
reverted: it opened every serial port on the machine uninvited, and it
belongs beside the thing that uses boards.

Proof: `ideconnect.script` (a desktop never told to look: Connect lists
both DKs after the project opens, Run reads as going to the chosen one,
looking again duplicates nothing) and every earlier board script
rewritten to the Connect flow -- iderunboard, stop, input, save, the
two-board script, and the live-namespace one -- all green.

Three script facts that cost a round each: Escape does not close a menu
here, a click on the window body does; clicking another title while a
menu is open switches to it; and `menu-pick` searches only the OPEN
menu, exact label first, then substring -- so "Save on tiku" and "Run
blink.bas on tiku" must be named whole.

