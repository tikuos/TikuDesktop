# Diagnostics: when the desk stops, say what it was doing

*2026-09-09.  A plan, not yet built.*

## 1. What goes wrong today, and why nothing says so

Everything that went wrong in the board-and-bench work fell into four
shapes, and the desk had nothing to say about any of them:

1. **A freeze.**  The process is alive and answers nothing: a scan that
   waited the full time on every port inside the event loop (21 s); a
   duplicate mount that put two owners on one cable; a streamed run that
   sat in SENDING for ever.  Not a crash -- no signal, no exit -- so no
   crash handler would have caught it.  What was needed was "the loop
   has not turned in five seconds, and the last thing it began was
   `look: probing /dev/ttyACM2`".
2. **A crash.**  A double close after `tiku_mount_remove`; a service
   pointer read as garbage because `start()` had the wrong signature.
   SIGSEGV, gone; the only trace was a gdb session I ran afterwards.
3. **A silent death of an application.**  Applications run out of
   process through `tiku-run`.  When Bench segfaulted at start the desk
   saw only "a session closed" and showed nothing at all; the signal
   that killed it was thrown away by nobody calling `waitpid` for it.
   It was found only by loading the .so IN process so the crash took
   the desk down where gdb could see it.
4. **Wrong, but silent.**  A menu row that did nothing because its
   command number was at or above `CMD_WINDOW`; an exec that failed
   because `argv[12]` overflowed, reported as "the run said nothing".
   Nothing crashed and nothing stalled; the sequence of what was asked
   and what answered was the only thing that could have said why.

The desk has today: no signal handler, no backtrace, no record of
recent events, no capture of a runner's exit status, and its stderr
goes to `/dev/null` under the harness and nowhere in particular when
launched from a menu.  Every one of the four was diagnosed by adding
`fprintf` and running again -- which a person using the desk cannot do.

## 2. The shape of the answer

Four pieces, in the order they pay back.  The rule under all of them:
**recording is cheap and always on; writing to disk happens only when
something has gone wrong, or is asked for.**  A diagnostic that costs
the hot path anything will be turned off, and one that is off is not
there when it is needed.

### P0 -- the trail  (DONE 2026-09-09)

A fixed ring in the desk's memory -- 512 entries of one short line
each, no allocation, no I/O -- appended at the moments that turn out to
matter: a turn beginning and the event kind it took; a menu pick and
its command; a wire message arriving and its type; a session arriving
or dropping, with the runner's `waitpid` status when it dropped; an
application starting or stopping; a mount added or removed and why; a
look beginning, each port it probes, and the result; a run's every
state change (SENDING, RUNNING, ASKING, ENDED, STOPPED, FAILED); a
release and a reclaim.  Each line carries milliseconds since the desk
started.  `trail(app, "look: probe %s", port)` -- one call, that is the
whole API.

This alone would have named all four shapes above, because the last
line of the ring IS the answer to "what was it doing".

**Proof:** `trail.script` does a run and reads the ring back through a
new `expect-trail <substring>` verb (the script engine already asks the
desk questions through `say()`); asserts the run's states appear in
order.

### P1 -- the crash report  (DONE 2026-09-09)

`sigaction` for SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT, installed
by the desk AND by `tiku-run` (they share the kits, so it is one file:
`kits/support/tiku_report.c`).  The handler is async-signal-safe: it
`write()`s, to `$HOME/.config/tracker/reports/crash-<time>-<program>.txt`,
a header (program, version and commit, argv, the time), a backtrace
(`backtrace_symbols_fd`, glibc), and the trail ring; then restores the
default action and re-raises, so a core still drops for gdb.  Nothing
is formatted with `printf` in the handler; the trail's lines were
formatted when they were written.

The desk also learns WHY an application died: the runner's exit status
is collected (`waitpid`, `WIFSIGNALED`) when its session closes, goes
into the trail as `app Bench died: SIGSEGV`, and is SAID -- an alert
naming the application and the signal, with the report's path -- so a
vanished window is never silent again.

**Proof:** the dev build only (`-DTIKU_TRK_FAULTS`, never in a shipping
build) honours `TIKU_TRK_FAULT=segv|abort|stall-8s` from the
environment, and an application `apps/tests/fault_app.c` that
segfaults in `start()` when asked.  `crash.script` launches the fault
app and asserts with `expect-file-has` that a report exists naming
SIGSEGV and containing `start`; `expect-alert` that the desk said so.

### P2 -- the stall watchdog  (DONE 2026-09-09)

The desk is one thread and a freeze is a turn that does not end.  A
watchdog thread -- the one place a second thread is right, because it
touches nothing but a counter -- watches a heartbeat the loop bumps
each turn.  No bump for 5 s: it writes `stall-<time>.txt` with the
trail (whose last line is what the turn began) and the main thread's
backtrace, taken by `pthread_kill(main, SIGUSR1)` whose handler writes
it; then goes on watching, and writes again at 30 s and 120 s so a
long stall leaves a record of being long.  It never kills anything: a
scan that takes six seconds is a stall to record, not a process to end.

**Proof:** `stall.script` with `TIKU_TRK_FAULT=stall-8s` (a turn that
sleeps eight seconds); asserts the stall report exists, names the
seconds, and its backtrace contains the sleeping function's name.

### P3 -- a supervisor  (DONE 2026-09-09)

`tiku-desk`: a small launcher that runs TikuDesktop with stderr
captured to `reports/desk.log` (rotated at 1 MB), waits for it, and on
an abnormal end writes `exit-<time>.txt` with the status, the signal,
and the last 200 lines of stderr; then offers, once, to start it
again.  This is the "service that keeps track": the thing that is still
standing when the desk is not, and the one place a person launches
from.  It is the only new process.

**Proof:** `supervisor.sh` (a host-side test): kills the desk with
SIGKILL and asserts `exit-*.txt` names it.

### P4 -- a report on demand  (DONE 2026-09-09)

SIGUSR2 to the desk writes `report-<time>.txt`: the trail, and a
snapshot of what is safe to read -- the mounts and their ports, the
channels, the sessions and their applications, a run in progress and
its state, a look in progress and where it is.  **About this system**
gains one row, *Last report…*, that opens the newest file in the
reports folder in the editor.  That is the whole of what the desk shows
for this: a folder of plain text files, and one row that opens the
latest.  Nothing pans the desktop.

**Proof:** `report.script`: sends SIGUSR2 through a new script verb,
asserts the file names the mounted board and the running application.

## 3. What is deliberately NOT in this plan

No telemetry, nothing leaves the machine.  No symbolisation beyond
`backtrace_symbols_fd` -- `addr2line` on the report is the person's to
run.  No attempt to recover from a crash in the handler.  No diagnostic
that formats, allocates or locks in the signal handler.  No fault knob
in a build that ships.  No changes to the applications' ABI: an
application reports its own crash through the runner, which is
already the desk's.

## 4. The rule, kept from the freeze

*Nothing that opens a serial port runs in the desk's loop for longer
than a beat.*  P2 is how that rule is enforced rather than remembered:
a turn that takes five seconds is written down, with what it was.

## 5. Order and cost

P0 first: it is half a day and every later piece writes it out.  Then
P1, because a crash with no report is the worst of the four.  P2 next,
since freezes were the most frequent shape here.  P3 and P4 after, in
either order.  Three and a half days in all, each stage landing with
its proof.

## 6. What P0 settled, and where it differs from the plan above

The ring is `kits/support/tiku_trail.{h,c}`: 512 lines of 112 bytes,
formatted when written so that reading needs nothing but `write()`.
`tiku_trail("look: probe %s", port)` is the whole API.  It is read as a
noun the shell already answers -- `expect-read trail:<word> <needle>` --
so no new script verb was needed.

Three deviations, each for a reason:

1. **No `app` argument.**  The plan wrote `trail(app, ...)`.  The ring is
   one per process and the crash handler will have no pointer to
   anything, so it is a global in the support kit -- which also lets
   `tiku-run` use the same file when P1 lands.
2. **Not every turn.**  "A turn beginning and the event kind it took"
   would have let a few seconds of mouse movement flush the ring and
   take the history with it.  What is recorded instead is what CHANGES
   something: launches and exits, menu picks, wire messages, mounts and
   unmounts, every port a look probes, every state a run passes through,
   releases and reclaims.  The "what is the current turn doing" line
   belongs with P2's watchdog, which is what wants it.
3. **A filtered read.**  A reader's buffer is 2 KB and the ring is 55 KB,
   so `trail:<word>` answers with the part of the trail about that word.
   Without it an assertion about a launch could not survive the run that
   came after it.

**And one thing found by writing it:** nothing collected a launched
program's exit status -- `tiku_trk_launch_run` reaps with WNOHANG the
instant after forking, which almost always returns 0.  So an application
that died left no word AND its zombie stayed in the table.  The notice
pump now reaps whatever has exited and writes down how it went, which is
half of what P1 promised, arriving early.

**Proof:** `test_trail` (10 legs: order, what a full ring drops and
keeps, a small reader gets the newest, a filtered reader gets only its
own, and the signal-handler road gives the same lines) and
`trail.script`, which drives a real run on a board and reads the
sequence back -- the mount, the launch, the look, the ask arriving over
the wire named `brun`, and the run from `start on tiku` to `ended`.  The
three run assertions were confirmed to FAIL on a session where nothing
is run, so they are not vacuous.

## 7. What P1 settled

`kits/support/tiku_report.{h,c}`, armed by the desk in its `main` and by
`tiku-run` before it loads anything, named for the APPLICATION rather
than the runner.  A fault writes `crash-<when>-<program>-<pid>.txt` and
the same bytes to `latest-crash.txt`, then restores the default action
and re-raises -- so the program still dies of what killed it and a core
still drops.

Everything a handler cannot safely do is done at arming time: the path,
the header, the program's name, and one throwaway `backtrace()` so the
unwinder has already loaded what it needs.  What the handler itself does
is open, write and close.

The report reads, in this order: the program, when it started, the
signal, **the last thing it did**, where it was, and then the whole
trail.  The last line is quoted at the top because that is what a reader
wants before forty frames of backtrace -- and because a reader with a
512-byte window (the harness has one) would otherwise never reach it.

And a dead application is no longer silent: the desk collects its exit,
writes it down by name, and SAYS it -- "Fault.so stopped: signal 11.
What it was doing is in .../latest-crash.txt".

**Deviations:** the plan's `TIKU_TRK_FAULT=segv|abort` knob was not
built.  A fault knob inside the shipping program is a thing that can
fire by accident; an application that faults on purpose
(`build/tests/Fault.so`, built with the tests and never installed beside
the real ones) tests the same path from outside, which is where the
fault it is imitating comes from.  `TIKU_TRK_REPORTS` does exist, so a
test can say where reports go.

**Proof:** `test_report` (9 legs: the report exists, names the signal
and the program, the backtrace reaches the faulting function, the trail
is there oldest-first, the summary precedes the detail, and the program
still dies of the signal), `test_reap` (7), and `crash.script` -- an
application that faults inside `start()`, before it has a window, where
the only word of it is what the shell says.

**A limit, stated:** static functions appear as module and offset, not
by name.  `addr2line -e <module> <offset>` resolves them; that is the
person's to run, as this plan said it would be.

## 8. What P2 settled

`kits/support/tiku_watch.{h,c}`: the desk bumps a counter each turn, and
a thread that touches nothing else watches the count.  Quiet for five
seconds and it ASKS the stuck thread, by SIGUSR1, to write the report --
so the backtrace is the stuck thread's own, and the trail is read by the
thread that writes it, which is what makes the ring safe to read at all.
If the stuck thread cannot answer within a second (a syscall the kernel
will not interrupt), the watcher writes what it has: the trail, without
a stack.  Then again at 30 s and 120 s, so a long stall leaves a record
of being long.

It never ends anything.  A scan that takes six seconds is a stall to
write down, not a program to kill -- and a shell killed by its own
watchdog would lose the very thing somebody wanted to read.

**No fault knob, again.**  An application loaded INTO the shell's
process holds one tick for eight seconds (`build/tests/Stall.so`, built
with the tests).  That is a real freeze of the real loop, arranged from
outside, rather than a switch inside the shipping program.

**Three things the building taught:**

1. **A signal cuts short a sleep.**  `nanosleep` is never resumed after
   a handler, whatever `SA_RESTART` says.  Asking a stuck thread to
   speak will therefore shorten a sleep it was in -- which a waiting
   loop must expect, and which made the first test's "stuck" turn end
   early and race its own assertion.
2. **A static function has no name in a backtrace.**  The same lesson as
   P1: the thing a report is asked about must have external linkage, or
   it is an offset for `addr2line`.
3. **The harness read 512 bytes of a file.**  That is where the useful
   part of a report begins, not ends; it now reads 8 KB, which is what
   let the stall test assert the stuck function by name rather than
   asserting that something went wrong in general.

**Safe beside fork():** the watcher never allocates, and the child
between `fork` and `exec` only calls `getenv`, `chdir` and `execvp` --
so the classic hazard of forking a threaded program is not reached.

**Proof:** `test_watchdog` (10 legs: a turning loop is left alone and
nothing is written about it; a stopped one is noticed, named, timed,
carries the STUCK thread's backtrace and the trail; and a loop that
turns again goes quiet) and `stall.script`, where an application holds
the shell's own turn for eight seconds and the shell is still answering
afterwards.

## 9. What P3 settled

`apps/desk/tiku_desk.c` -> `build/tiku-desk`: starts the desk beside it
(or `$TIKU_DESK`) with stderr kept in `reports/desk.log` (rolled at
1 MB), waits, and on a bad end writes `exit-<time>.txt` -- the signal
or status, the desk's own crash or stall account if it managed one,
the last 200 lines it printed -- and points `latest-exit.txt` at it.
Then it starts the desk ONCE more with `-after <that file>`, and the
desk's first act is to say so on its screen, with the path.  A desk
that ends badly again is left down, with the reason on the
supervisor's stderr.  SIGTERM/SIGINT/SIGHUP to the supervisor go to the
desk and that end is not mistaken for a mishap.

**Two things the building taught:**

1. **The supervisor found the wrong desk and was right about it.**  The
   shipping desk beside `build/tiku-desk` has no script engine; it
   refused the test's script with exit 2, and the supervisor wrote
   that down, restarted once, and stopped -- the whole mechanism
   working, on the wrong program.  The harness names the development
   desk with `TIKU_DESK`, as it names applications with `TIKU_APPS`.
2. **A Restart must forget `-after`.**  The Deskbar's Restart re-execs
   the same argv; carried through, the word "stopped and was started
   again" would greet a restart nobody crashed from.  `restart_argv`
   is now the argv less that pair.

**Proof:** `TikuTracker/tests/supervisor.sh` (18 legs, host-side, run
by `make test`): killed from outside with SIGKILL, which no handler
inside the desk can record, the exit report names signal 9; the desk
is started again and told where the account is; the restarted desk
SAID so; killed twice it is left down with two reports and no desk
behind; ending the supervisor ends the desk and writes nothing.

## 10. What P4 settled

SIGUSR2 only ASKS: a handler can format nothing, so it sets a flag and
the loop writes `report-<time>.txt` on its next turn, where everything
may be read -- the mounts with their ports, the channels, the sessions
by name with their windows, the applications hosted in this process, a
run in progress and its state, a look in progress and where it is, and
the trail.  `latest-report.txt` points at the newest.  The script verb
`report` raises the real signal.

**About this system** gained its one row: *Last report: <kind>,
<moment>*, in the link colour, from the newest file in the folder.  A
click opens it as a double click would -- and when nothing on the
machine claims text files (a fresh home has no application registry,
and the built-in "Open with Editor" add-on only sets a status line), it
opens the reports folder instead, with the name on the row to find it
by.  That needed one service, `open_path`, at the end of the services
table; the runner leaves it NULL, since an application out of process
is not on this machine's paths.

**Three things the building taught:**

1. **Channels do not share the mounts' slots.**  The first snapshot
   said "/, a channel" -- indexing `channels[]` by mount slot.  They
   are listed apart now, by descriptor and losses.
2. **A report cannot contain the line that says it was written.**  The
   trail goes into the file before the "report written" line is
   trailed; the script asserts the launch before it instead.
3. **A file name runs off the box.**  The row says the kind and the
   moment parsed from the name, not the name.

**Proof:** `report.script` on hardware -- a mounted board and a running
TikuBasic; the file names `/devices/tiku` with its port, the session,
`asked for: SIGUSR2`, the launch in the trail; the trail noun knows;
About opens, its row is clicked, the reports folder opens.

