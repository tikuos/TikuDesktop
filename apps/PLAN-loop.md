# The loop never waits: workers for I/O, one owner for state

*2026-09-09.  A plan, not yet built.*

## 1. What goes wrong today, and why it keeps happening

The desk is one thread, and every slow thing it does is done on that
thread.  The first freeze found -- a port scan that waited the full
time on every serial port inside the event loop, 21 s -- was fixed by
hand: the scan became a state machine that probes one port per beat.
That was the wrong fix, and it took a day.  The stall watchdog (P2 of
the diagnostics plan) can now SAY when the loop has stopped turning,
which makes the question "what else waits?" answerable; an inventory
of the tree answers it, and the answer is not three places but thirty.

The ones that matter, by how often they fire:

1. **The notice pump, every 400 ms, asks every mounted board what
   changed** -- `tk_changes` -> `tiku_ns_events` ->
   `tiku_session_cmd(.., 4000)`.  A live board answers in
   milliseconds; a board unplugged but still mounted costs the full
   four seconds, every 400 ms.  The desk then spends 4 s in every 4.4
   in `poll()` on a dead file descriptor, one second short of the
   watchdog's five, so it is a freeze that never even gets written
   down.  And a record that is not `changed` re-reads the whole
   manifest inline, at 15 s a page.
2. **Opening a folder fetches the whole listing in one call**
   (`tiku_trk_poseview_load_begin`, `ops->list`).  Everything below it
   is incremental -- the chunked publishing, the barber pole -- but it
   only staggers the DRAWING of rows already fetched.  A device folder
   is a 6 s freeze; a local folder of a hundred thousand entries is a
   `readdir`+`stat` freeze.
3. **A look, a reclaim, a board dialling in** -- 800 ms per silent port
   (up to 16 of them for a reclaim, in series), then a manifest load
   and an identity read for every board that answers, 10-30 s for a
   live one.  "One port per beat" bounds the dead-port case only.
4. **The file engine copies a whole file inside one step**
   (`copy_bytes`); a step budget of 4 counts items, not bytes.  A
   four-gigabyte copy is the loop gone for the whole copy.
5. **The small ones**, each a freeze on its own day: a thumbnail is a
   whole image decoded per turn; the first sound after five seconds'
   quiet reconnects to PulseAudio on the draw path; `statvfs` on every
   mount every 400 ms, which on a hung network mount blocks for ever;
   `umount(2)` from the Unmount menu flushes a USB stick's dirty pages
   on the loop; a device panel's control writes with a 12 s ceiling.

Each of these was, or will be, "fixed" the way the scan was: by
chopping it into pieces small enough to hide between frames.  That is
work spent to save a resource the desk does not lack.  TikuDesktop runs
on a machine with cores and a scheduler; the microcontroller reflex --
never block, never thread, chop everything -- is the board's
discipline, not the desk's.

## 2. The shape of the answer

**Threads freely, for anything that waits; the loop keeps ownership of
everything that matters.**  The rule the desk has actually been living
by is not "one thread" but *one owner of the state*: mounts, windows,
sessions, the running program are touched by one thread, so no lock
exists and none can be got wrong.  Every bug in the board-and-bench
work was a logic bug and not one was a race; that property is worth
keeping, and it has nothing to do with how many threads exist.  It is
about what a thread is allowed to touch.

So: a **worker** takes a self-contained request -- "open this port,
does a board answer?", "list this folder", "copy this file", "decode
this image" -- touches no desk state, and returns a **result** the loop
applies on its next turn.  No locks on mounts, windows or sessions; no
drawing from a worker; no Xlib from a second thread.  This is how every
serious desktop toolkit is built (one UI thread, workers that report
back), so it is not a compromise.

Three facts that make it cheap here:

- **The loop already turns every 10 ms** on its own clock
  (`desktop_turn_end` naps 10 ms, or until the next task is due).  A
  worker's result does not need to WAKE the loop; it needs a queue the
  loop drains each turn.  Result latency is at most one nap.  No
  self-pipe, no `eventfd`, no `XSendEvent`, no change to
  `tiku_host_poll`.  (Blocking on the X socket instead of napping is a
  power nicety for later, not a freeze fix; it is listed at the end.)
- **The ask-then-result shape already exists.**  A look is begun by a
  wire message, parked in `app->look.session`, and answered beats later
  by `desktop_board_tell(.., TIKU_APP_BOARD_LOOKED, ..)`.  Release and
  reclaim answer in the same turn from a return value; they become the
  deferred shape the look already has.
- **The watchdog is the proof.**  A stall is now a thing the desk can
  be asked about.  Every piece below is proven by a script that arranges
  the slow thing and asserts the loop kept turning -- and is shown to
  FAIL with the slow thing back on the loop.

## 3. The rules a worker lives by

1. **A job touches nothing it was not handed.**  Its context is copied
   or owned outright at dispatch; a pointer into `desktop_t` is not a
   thing a job may hold.
2. **A job never draws, never posts to a session, never touches a
   mount.**  It computes, waits, reads a file descriptor it owns, and
   fills its result.
3. **A board's wire has one hand on it.**  A serial session (an fd with
   a prompt discipline) is used by one thread at a time.  While a job
   holds a session the loop does not; jobs for one board are dispatched
   one at a time, in order, and the loop keeps the order.  This is the
   one place the "one owner" rule moves: the owner of a wire is
   whichever thread is in the middle of a command on it.
4. **A result names its generation.**  A mount slot can be reused while
   a job for it runs; a job carries the mount's generation and the loop
   drops a result whose mount has since gone.  A cancelled job's result
   is dropped the same way; cancellation is a flag the job checks, never
   a kill.
5. **The loop applies results in `desktop_turn_begin`**, before it takes
   events, so the screen a click lands on already shows what arrived.
6. **A job never allocates from, or frees into, desk state.**  It may
   `malloc` for itself; ownership of what it made passes to the loop
   with the result.
7. **The watchdog's beat stays on the loop.**  A worker that beat it
   would hide the very stall it exists to find.
8. **`fork()` stays exec-only in the child** (`getenv`, `chdir`,
   `execvp`): the desk is now a threaded program and the classic
   fork-after-threads deadlock is one `malloc` in the child away.

## 4. The pieces, in the order they pay back

### W0 -- the worker kit  (DONE 2026-09-09)

`kits/support/tiku_work.{h,c}`.  A pool of four threads and two queues:
requests in, results out.  `tiku_work_post(&job)` from the loop; a job
is `{ run(ctx), done(ctx), free(ctx), ctx, key, gen, cancel }`.  A
worker takes a job, calls `run` off the loop; the loop calls
`tiku_work_collect()` each turn and it runs `done(ctx)` for every
finished job, on the loop, then `free`.  `key` serialises: jobs sharing
a key run one at a time in the order posted (rule 3, a board's wire).
`tiku_work_cancel(key)` marks and the loop drops.  `tiku_work_busy()`
for the trail and the idle gate.  The pool starts on first post and
never ends.  Nothing in it allocates on the worker side; the queues
are fixed rings.

Every post and every collect goes into the trail: `work: post look
/dev/ttyACM4` / `work: done look /dev/ttyACM4 (812 ms)`, so a report
says what was in flight.

**Proof:** `test_work` -- a job that sleeps is collected on the loop
side; two jobs with one key run in order and never overlap (a shared
counter that would race proves it did not); a cancelled job's `done`
never runs; a job posted from `done` is legal; `busy` is right.  And
the hazard leg: a job that beats the watchdog is refused (rule 7).

### W1 -- the board wire off the loop  (two days)

The biggest payback and the deepest change.  Every place the desk
waits on a board becomes a job with the board's key:

- **The notice pump's `changes`.**  A job per mounted board, one in
  flight per board; the pump posts one if none is in flight and applies
  the last result.  A dead board costs a worker four seconds and the
  loop nothing.  The inline manifest re-read on a non-`changed` record
  becomes a job of its own.
- **The look.**  `desktop_board_look_begin` posts a probe job per port,
  keyed per port so they run in PARALLEL; each result mounts or does
  not, on the loop; the last one tells the session.  The one-port-per-
  beat state machine (`look.at`, `look.going`, the step on the 75 ms
  beat) and the synchronous `desktop_board_look` are deleted.  A look
  now takes as long as the slowest port, not the sum.
- **A reclaim** is the same probe jobs with a uid to match; a board
  dialling in (`boards_accept`) opens on a worker.
- **Boot.**  `mount_devices` becomes the look, begun before the splash
  and applied as results land; the splash's stages are honest about
  it.
- **The panels and the device folder.**  A control's write and a
  node's read are jobs; the panel shows the value it asked for greyed
  until the result says so.  `tiku_ns_pump` re-reading every subscribed
  node without a deadline becomes a job per pump.

What stays: the session, the namespace, the channel -- untouched.  They
are called from a worker instead of from the loop.  What changes is
who calls them and when.

**Proof:** `deadboard.script` -- a mount whose port never answers (a
pty the harness holds open and never writes to).  The trail noun gains
`turns` (from the watchdog: the longest gap between beats in the last
few seconds) and the script asserts `expect-read turns` under 100 ms
across a look, a reclaim and ten notice pumps with the dead port
mounted.  With W1 reverted the noun reads 800 and more, and the script
fails: that is the break-it leg.  `ideconnect`, `iderunboard(s)`,
`benchrun`, `boardnet` unchanged and green on hardware.

### W2 -- a folder lists on a worker  (one day)

`load_begin` posts the `ops->list` as a job keyed by the window; the
staged listing is the result; `load_step` publishes it as it does
today.  `tiku_trk_poseview_reload`'s drain-it-all loop goes; a reload
is a new job and the old one is cancelled by key.  The barber pole
now means what it says.

**Proof:** `bigfolder.script` -- a folder of 200 000 entries made by the
setup; open it; `expect-read turns` under 100 ms while it fills;
`expect-title` shows the count arriving.  And the device folder in
`ideconnect` opens without a stall.

### W3 -- the file engine on a worker  (one day)

The whole of a `tiku_fs` operation runs on one worker; the loop sees
progress results (bytes, items, the current name) and the cancel flag
the engine already checks between chunks.  `copy_bytes` need not
change; it is simply no longer on the loop.  The progress window reads
the last result.

**Proof:** `bigcopy.script` -- copy a 2 GB sparse file; `expect-read
turns` under 100 ms; cancel it halfway and assert the target is gone.

### W4 -- the small ones  (one day, all together)

A thumbnail is a job per image (the decode; the write stays on the
loop).  The sound device opens on a worker; a sound asked for before it
is open is dropped, not waited for.  `tiku_volumes_scan` runs as a
periodic job and the loop reads the last scan, so `statvfs` on a hung
mount hangs a worker.  Unmount posts the `umount` and the Deskbar shows
the volume as going until the result.  `getaddrinfo`+`connect` in
`tx_dial` -- not reachable from the desk today, but on the shared API
-- is marked as worker-only in its header.

**Proof:** `hungmount.script` -- a FUSE mount whose `statfs` sleeps (a
twenty-line script the setup starts); `expect-read turns` under 100 ms
across ten pumps.  Thumbnails and sound by the existing scripts.

### W5 -- later, and optional: block instead of nap

`tiku_host_wait(host, ms)` on the X socket plus a self-pipe the worker
kit writes on `done`, so an idle desk takes no wakeups instead of a
hundred a second.  This is power, not freezes; it changes nothing
above and needs nothing above to change.  Do it when a laptop battery
is the complaint.

## 5. What is NOT changing

- **Applications stay out of process.**  That is an isolation decision
  -- a crashing application does not take the desk -- not a threading
  one, and it stands.
- **The loop is still the one owner** of mounts, windows, sessions and
  the running program.  A worker never touches them; it hands back a
  result.
- **Xlib stays single-threaded.**  No `XInitThreads`, because no worker
  draws.
- **The watchdog stays**, and becomes the instrument: `turns` is how
  every piece above is proven.
- **The task system (`tiku_trk_loop`) stays** for things that are
  cheap and timed; a worker is for things that WAIT.

## 6. The traps, named before they are met

1. **A session used from two threads.**  The bug will look like a
   prompt matched against the wrong command's output.  Rule 3 and the
   job key exist for this; the notice pump is the place it will be
   broken first, because it fires while a look is probing the same
   board.
2. **A result for a mount that is gone.**  Unplug during a look.  The
   generation (rule 4) is the guard; the test is the unplug in
   `deadboard.script`.
3. **A job holding a pointer into a window that closed.**  W2's key is
   the window; closing cancels by key and the loop drops the result.
   Never the other way round -- a window must not wait for its job.
4. **`fork()` after threads.**  The launch path is exec-only in the
   child today; a `printf` added there for debugging would deadlock
   one run in a thousand.  Rule 8, and a comment at the fork.
5. **Draining a queue that is being filled.**  `tiku_work_collect`
   takes a snapshot of the result ring's head and drains to it, so a
   job posted from `done` lands next turn rather than looping.
6. **The stall test that is not a stall.**  A signal cuts short a
   `nanosleep` (learned in P2): a job that "waits" by sleeping and
   returns early on SIGUSR1 has proven nothing.  Waits in tests are on
   a clock, not a sleep.

## 7. What W0 settled

`kits/support/tiku_work.{h,c}`: a fixed table of 64 slots under one
lock, four workers started on the first post.  A worker takes the
oldest QUEUED slot whose key has nothing RUNNING; the loop takes the
FINISHED slots at collect -- those finished at ENTRY, so a job posted
from a done lands next turn rather than looping -- and runs done, then
free, on the loop.  Cancel marks; a marked job not yet started is
finished without running, a running one sees `tiku_work_cancelled()`
and may stop; neither has its done called, both are freed.  Nothing
in the kit allocates.  The desk collects at the top of every turn,
before events.  Every post, done and drop is a trail line with how
long the job ran and how long it waited.

The watchdog gained what the proofs read: `tiku_watch_longest_ms()`,
the longest gap between beats, answered by the desk as the `turns`
noun (and reset by the reading, so a script measures the stretch it
arranged); and a beat from any thread but the watched one is refused
and counted, so a job cannot hide a stall.

**Proof:** `test_work`, 29 legs, each guard shown to FAIL without it:
without key serialisation two jobs under one key ran at once; with a
collect that kept draining, the job posted from a done was collected in
the same turn; with beats accepted from any thread, the worker's beat
counted.  (A first mutation of the collect rescanned BEFORE running
done and proved nothing -- the mutation, not the test, was wrong; the
one that rescans after is the one that fails.)

