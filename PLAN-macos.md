# TikuDesktop on macOS

*A plan for a second host: the desktop, the browser, the applications and
the whole test run on macOS, with the Linux build left exactly as it is.
2026-09-14.*

## 1. Is it possible

Yes, and it has been measured rather than guessed.  A trial port in a
scratch copy of this tree -- sixteen files, +426/-31 lines, kept at
`tikuOS/temp/macos-port.patch` -- gives, on an Apple Silicon machine with
the command-line tools, Homebrew's `libx11` and no X server at all:

| | |
|---|---|
| `make -j8` from clean | 0 errors in ten seconds: all seven kits, both shells in both flavours, every application and its loadable form, 134 test binaries |
| the `make` gates | all green: architecture, kits, font hash, dl ops, board metrics, behaviour files, toolkit, the 889-row contract, menu commands, shipping, style |
| the unit suites | 125 of 134 pass |
| the driven scripts | 248 of 265 runs clean; 1373 assertions pass, 38 fail |

Three findings shape the plan:

- **The tree is already almost portable.**  X11 is three files; there is
  no Linux header in use but `<sys/xattr.h>`; the errors were spellings
  (`st_mtim`, `memrchr`, `B460800`, `pty.h`, a Mach-O section name) and
  one structural difference (the mount table).  What remains is not
  compile errors but *features that read `/proc`* and *tests that assume
  a Linux box*.
- **The harness needs no display.**  The scripts run with `DISPLAY=`
  empty and read pixels from the offscreen surface, so the whole of
  `make test` can pass on a Mac with no X server.  XQuartz is for a
  person, not for the build.
- **A second platform is a bug detector.**  Two real defects surfaced
  that Linux hid: `tiku_draw_open()` leaves half its window struct
  uninitialised and reads a wild pointer on the greeting (quiet only
  because both callers are `static`); and `test_device_utilities.c`
  uses a monitor after `desktop_utilities_destroy()` freed it.  Neither
  is a macOS bug.  Both are fixed in the patch.

The rule that keeps Linux unchanged is stated once and checked by a
tool: **no `#ifdef __APPLE__` in a shell, an application or a kit's
logic.**  Where the platforms differ in *spelling*, one forced header
(`kits/support/tiku_darwin.h`) says so and no source names a platform.
Where they differ in *mechanism* -- the process list, the mount table,
the sound card, a symboliser -- the mechanism lives behind a seam in
`kits/support` or `kits/device` with one file per platform, chosen by
the Makefile, and the Linux file is the existing code moved whole, not
rewritten.  A checker refuses `/proc/`, `__linux__` and `__APPLE__`
anywhere else.  On Linux the objects that matter are built from the same
lines they are today.

## 2. Milestones

| # | Milestone | Scope | Cost | Done when |
|---|---|---|---|---|
| **0 ✓** | The build | Apply the trial patch: the Darwin block in `mk/toolchain.mk` (find X11 in `/opt/X11` or Homebrew, `-D_DARWIN_C_SOURCE`, the forced `tiku_darwin.h`, `GC_SECTIONS`, `RT_LIBS`); the Mach-O stamp `__TIKUOS,__tikuos` and its reader beside the ELF one in `tiku_backend_posix.c`; `tiku_state.c`'s xattr calls wrapped once for both signatures; `tiku_volume.c` on `getmntinfo()`, `MNT_DONTBROWSE`, `unmount()`, and the sealed-root rule (`/` wears `/System/Volumes/Data`'s flags, since both answer to one `st_dev`); `SO_NOSIGPIPE` on the TCP link; `"\x01" "desk"`; `<util.h>` in three tests.  The two bug fixes: `memset` in `tiku_draw_open()`, and the use-after-free in `test_device_utilities.c` (keep the monitor's fields the test wants in locals before the destroy).  `README.md` gains a macOS paragraph: what to install, that no X server is needed to build or test | half a day | `make` clean on both hosts; on Linux, `git diff --stat` of the objects' sources shows only the two bug fixes and guarded additions; 126+ of 134 unit tests on macOS |
| **1 ✓** | The harness, portable | `run.sh` resolves `/tmp` once with `pwd -P` and reads each script through one substitution so the name it asserts is the name the shell reports back (the reader, the IDE and the launcher all call `realpath()`); on a host where the two already agree -- every Linux -- the script runs from where it lies, untouched.  `tests/second-volume.sh` yields a volume for the Trash, unmount and cross-mount lanes: `/dev/shm` on Linux, and on macOS a ram disk from `hdiutil attach -nomount ram://` erased with `diskutil eraseVolume`, which wants no privilege; opened once by `run.sh` and by `make test`, given back under a `trap` however the run ends.  `set_xattr` in `run.sh` for `setfattr` vs `xattr -w`; `dd`'s seek for `truncate -s`; a `$skip` a fixture sets when it cannot lay out what its script needs.  Three headers for what a host must have: `# host: shallow-tmp`, `# volume: not-mine`, and a `pty_room()` probe in `test_link`/`test_remote`.  `test_document_reader`, `test_ide_workflow` resolve their own temporary; `test_document_association` runs itself by `realpath(argv[0])` rather than `/proc/self/exe`; `test_fs` and `test_volume` take the volume from `TRK_VOLUME2` | one day | `make test` green on Linux *unchanged in what it proves*; on macOS every suite and script that is not gated on a host mechanism passes, and each that is says `SKIPPED` with the reason on one line, never silently |
| **2 ✓** | The system seam | `kits/support/tiku_sys.h` and its two per-host files, chosen by `uname -s`: `tiku_sys_processes()` (pid, name, image -- this user's own, and only those with an image), `tiku_sys_memory()`, `tiku_sys_uptime()`, `tiku_sys_self_path()`, `tiku_sys_frame_name()`, beside the `tiku_sys_fault_pc()` already there.  The Linux file is the `/proc` code moved whole; the Darwin file is `proc_listpids`/`proc_pidpath`, `sysctlbyname` (`hw.memsize`, `kern.boottime`), `host_statistics64`, `_NSGetExecutablePath`.  Two of the six callers needed no seam at all -- `sysconf(_SC_NPROCESSORS_ONLN)` and `getloadavg(3)` are on both hosts, so the About window's CPU count and `tiku_trk_task`'s load meter are simply portable now.  `test_sys.c` asserts the contract against whichever host runs it.  `check_architecture.py` refuses `__linux__`, `__APPLE__` and a host `/proc` read outside thirteen named seam files, and nowhere under `TikuTracker/src`, `apps/` or `board/` | one to two days | the checker passes and a planted `/proc` or `#ifdef` in a shell fails it; About says the memory and the load on both; Restart execs a path the host gave it; `test_report` and `crash.script` name the faulting function on both |
| **3 ✓** | Sound | `kits/media/tiku_sound_coreaudio.c`: three buffers on an audio queue, the twin of the Pulse backend behind the same five calls, and `mk/kits.mk` compiles one of the two -- neither names a platform because neither is built on the other's host.  `tiku_sound_pulse_backend()` became `tiku_sound_host_backend()`, since four callers were naming a host to ask for sound.  The queue is primed with one buffer of silence before it is started: a queue started empty waits for an IO cycle that never comes, which is a hang rather than an error.  The framework is named once in `mk/toolchain.mk` with `-dead_strip_dylibs`, so `tiku-desk-probe` still references nothing.  `sounds.script` keeps the seven moments that need no volume; `soundbeep.script` takes the alert's beep, which was reached through an unmount refusal and wanted a volume this user may not unmount | one day | the moments pass on both hosts; the Linux build's `libtiku_media.a` has the members it had.  **Not heard from here**: this shell cannot start audio output at all -- `afplay` hangs on a system sound the same way -- so the backend is proven to build, open and take chunks, and `./build/system/tiku-sound-gallery` from a terminal is what proves the speaker |
| **4 ✓** | Disks and the way out | Force Quit: `tiku_sys_process_pin()`/`_kill()` join the seam, so the application kit's last two `#ifdef`s go.  Linux keeps pidfd; macOS pins with a kqueue on `NOTE_EXIT` for the descriptor's shape and re-checks the process's START TIME immediately before the signal -- a pid is handed out again, never with the same microsecond.  `posix_fallocate` was the other `#ifdef` and becomes `tiku_sys_reserve()`.  Disks: `tiku_partitions_scan()` asks DiskArbitration about each `/dev/disk*`, so the Mount menu has content where it was empty; `removable` is asked of the medium rather than guessed from the mount point, because "not the boot volume and local" calls a second internal volume removable and offering Eject on one is offering to break the machine; `tiku_volume_mount()` mounts through the same service, on a run loop bounded at five seconds so a disk that will not answer does not become a shell that will not answer | one to two days | `test_forcequit` passes, which it had not since the port began, and `forcequit.script` goes 10/13 to 13/13; a ram disk is listed, mounted and unmounted through the kit, and reads removable where the boot volume does not |
| **5 ✓** | Search that ends | Not what it looked like.  The engine was built to be stepped -- `tiku_query_step(r, budget)`, "advance the walk by at most @p budget directories" -- and **all three of its callers drove it to completion in a `while (!done)` loop on the turn**: the Find panel's search, the quick find, and `tiku_query_refresh()` inside the kit.  On a CI rootfs that is invisible; on a real disk the shell stops until the disk has been read.  `tiku_query_refresh()` is split into `_begin`/step/`_end` so the snapshot outlives the turn that took it, `tiku_trk_folder_lifecycle_step_results()` gives a live window sixteen directories a turn, and the desktop's idle work drives it beside the other stepped work.  A first walk shows rows as it finds them; a re-walk shows nothing until it ends, so a row about to be found again does not blink out.  The status line is left alone -- there is one of it for the whole desktop and a walk is thousands of turns | one day | `query`, `qtemplate`, `qtype` and `querydrop` finish in four to six seconds each, where two of them had to be killed; the whole script suite runs with no watchdog |
| **6 ✓** | A person at the desk | With XQuartz: `tiku_x11.c` reads the Retina scale from `Xft.dpi`/the screen's millimetres so the desktop is not drawn at half size; Command mapped to the desktop's command modifier where Control is today, from a table in one place (`tiku_event.h`'s modifier names), so the Linux keys stay as they are; the X11 clipboard proved against XQuartz's pasteboard bridge; the serial rates above 230400 set with `IOSSIOSPEED` when `tcsetattr` refuses them (`tiku_tx.c`, behind the seam); the IDE's message parser reads clang's diagnostic lines (`file:line:col: error:` is shared; the "note:" and caret lines differ) and `idebuild.script` runs with `CC=clang` on Linux too, so the parser is proved on both; `tiku-desk-probe` and the board link tried against a real board on a Mac, where a CDC device that has not answered EP0 blocks the open for thirty seconds and the desktop must not wait on it in the turn (open the port off the turn, as the utility jobs already are) | two days | `./build/TikuDesktop` on a Retina display at the right size, Cmd-key menus working, paste from another Mac application, a board connected over USB with its panel live; the Linux desktop's keys, clipboard and serial path unchanged, proved by the scripts |
| **7 ✓** | The bundle | *Optional.*  `make app` writes `build/TikuDesktop.app` (Info.plist, the mark as `.icns` rendered from the same drawing, the binary and `apps/` inside `Contents/Resources`, `TIKU_APPS` set by the launcher); `LSUIElement` off so it has a Dock tile; codesign ad hoc so Gatekeeper opens it on the machine that built it | half a day | double-clicking the bundle opens the desktop with its applications found |
| **8** | Native, without XQuartz | *A decision, not a default (§3).*  `kits/interface/tiku_cocoa.m`: the same `tiku_host` contract `tiku_x11.c` fulfils today and the wasm build replaces -- one window, a backing `CGImage` blitted per present, events translated to `tiku_event_t`, the pasteboard for the clipboard, `NSTextInputClient` for the input method; chosen by `TIKU_HOST=cocoa` in the Makefile, X11 remaining the default on both hosts until it is proved; the scripts do not know which host backend drew | three to four days | every script that passes under X11 passes under Cocoa; a Mac with no XQuartz runs the desktop |

Milestone 6 is built.  The half that was said to need XQuartz did not:
`xorg-server` is a Homebrew FORMULA, so it installs into a prefix the
user already owns and wants no password, where the XQuartz cask is a
`.pkg` and does.  It puts a real X server on the machine, and the
desktop runs on it.

Order: 0 then 1 (nothing else can be proved until the harness tells the
truth on both hosts), then 2, 3, 4 in any order, 5 whenever (it is not a
macOS milestone but macOS is where it bites), 6 when a person needs it,
7 and 8 on decision.  Six to eight days for 0 through 6.

### Where it stands (2026-09-15)

Milestones 0 to 5 are built.  On this machine -- Apple Silicon, the
command-line tools, Homebrew's `libx11`, no X server:

| | before | after 0 to 3 |
|---|---|---|
| the build | 0 errors | 0 errors, ten seconds from clean |
| the gates | all green | all green |
| the unit suites | 125 of 134 | **146 of 147** |
| the driven scripts | 17 of 265 runs failed | **1396 assertions pass, 10 fail**; 4 skipped out loud |
| `supervisor.sh` | 16 of 18 | **18 of 18** |

What is left is waiting on a milestone, not on a mistake:

- Nothing in the suite.  `idebuild.script` and `idec.script` were the
  last two and milestone 6 took them.  What is left of milestone 5 is a decision rather than a
  defect: a new search's scope is `everywhere` (`tiku_trk_find_init`),
  which is what Tracker's Find panel defaulted to, so the walk is the
  whole disk even though it no longer blocks.  Narrowing that default
  changes what a search MEANS and is the owner's call, not a port's.

And four say why they did not run: `dropfont` (no DejaVu Sans to drop),
`panelroot` (it counts the steps from the fixture up to the root, and
`/private/tmp` is one deeper), `unmount` and `sounds` (both want a volume
this user may NOT unmount; the ram disk the harness mounts is its own).

Six things the work turned up.  Four are nobody's platform and all four
are fixed; two were not what they first looked like.  One is a bug the
seam's own test found in the seam.  The last is a feature nobody calls.

1. **`send_all()` in `tiku_remote.c` spun forever on `EAGAIN`**, though
   its own comment said "a stalled peer forfeits the message".  It had
   never bitten because a Linux socket buffer swallows a whole frame; on
   a real serial line with a desktop that paused it would have hung the
   client inside a draw.  Now bounded by `poll()` at two seconds -- and
   when that passes the LINE is given up, not the message, because half a
   frame is worse than none: the far end reads what follows as a length.
   Closing the descriptor turns every later send into nothing (they all
   guard on `fd < 0`) and the read side reports the disconnect callers
   already handle.  The same spin was in the client's read path, mid
   message, and is bounded the same way.
2. **A crash report lost the faulting frame** when the function that
   faulted was a leaf: arm64 keeps no frame pointer for one, so the walk
   steps from the signal trampoline straight to the caller.  The first
   guess -- that clang had inlined it -- was wrong; `noinline` was
   already there.  It needed the signal's own context, which is the first
   thing POSIX leaves to the machine, so `kits/support/tiku_sys.h` and
   its two files now exist with `tiku_sys_fault_pc()` in them.  The
   report prints that instruction above the walk, under its own line.
   **Milestone 2 inherits a seam rather than inventing one.**
3. **A pty holds 1024 bytes on macOS** and several kilobytes on Linux,
   and both tests that use one write a frame and read it on one thread.
   `test_link` now hands the link a turn when the line is full, which on
   a host whose buffer swallows a frame never happens; `test_remote`
   puts a shovel thread where the desktop's own process would be, moving
   bytes between the pty and a socket pair the listener is adopted on.
   The session still crosses a real pseudo-terminal, which is the claim
   under test; what changed is that something is reading the far end, as
   something always is.  **This one changes the test on Linux too**, and
   deliberately: it models two processes, which is what there are.
4. **The seam caught a bug in its own Linux half.**  The roster offered
   `argv[0]` out of `/proc/<pid>/cmdline`, so a program started by a
   relative name was offered under one and a caller handed it could not
   find the file; the macOS twin gave an absolute image all along.  The
   assertion in `test_sys` that the image starts with `/` is what found
   it, and `/proc/<pid>/exe` is what fixed it -- which is the argument
   for a seam having a test of its own rather than being taken on trust.
5. **The Deskbar's process roster has no callers.**  `tiku_trk_deskbar_scan_processes()`
   is compiled into the desktop -- the other half of its file,
   `publish_workspace()`, is what keeps it linked -- but nothing calls
   it, on either host.  So "the Deskbar lists running programs" is not
   something this desktop does today; what milestone 2 did was make the
   function portable and give the machine underneath it a test.  Wiring
   it in is a product decision, not a port.
6. **`scroll.script` was not what the flake looked like.**  The autoscroll
   hold was said to be counted per draw and the scripted `wait` per turn;
   in fact both are per turn, and the band controller was never the one
   scrolling -- its folder is NULL throughout.  The drag TARGET does the
   autoscroll, and its hold was still `now - scroll_at <= 600 ms`, wall
   time, while its spring beside it had already been taught to count
   pulses.  So whether a script's twenty-five turns had used up six
   hundred milliseconds was the machine's load, run to run.  The
   autoscroll now counts turns under a script, exactly as the spring and
   the band do, and the script passes every time.

### What milestone 6 did, and what it is waiting on

Two halves, and only one of them needs a screen.

**The compiler.**  The IDE read gcc's diagnostics and no others, and the
fix was not a second parser.  The head both compilers share --
`file:line:col:` -- was already parsed correctly; what broke was
everything around it.  Under each complaint both quote the source and
point at it, as a numbered gutter and a row of carets, and the IDE was
listing those as MESSAGES: four rows for one error, three of them things
a person cannot act on.  They are folded away now.  And the two scripts
reached the error by counting `Down` presses from an unselected list,
which cannot work on both hosts at once: gcc names the function before
saying anything is wrong and clang counts the errors up after, so the
error's row NUMBER differs by compiler.  The messages window now opens
on the first row that NAMES A PLACE -- a failed build opens on its first
error -- which is the same row under both and needs no count.  Proved in
`test_ide_workflow` against the real output of each, so neither compiler
has to be installed to prove the other's, and the row a script lands on
is no longer a fact about the machine that built it.

**The board.**  Two things, both behind the seam, and one of them was
worse than the plan knew.

`tiku_sys_serial_speed()` sets a rate the ordinary call could not.  But
the plan said "when `tcsetattr` refuses them", and refusing is the
harmless case -- the call reports it.  The other case is a driver that
TAKES the rate and runs at something else, which nothing reports and
which looks exactly like a board that is not answering.  So both places
that configure a port now set the rate and READ IT BACK, and go to the
driver when the two disagree; `tiku_conduct.c` was discarding
`tcsetattr`'s result entirely and is the second of them.  Proved against
this machine's own serial driver: 460800 and 921600 are taken the
ordinary way, 3000000 is refused by `tcsetattr` and set by the seam.

`tiku_sys_serial_ports()` is the one the plan did not have.  Listing
ports went through `/dev/serial/by-id`, which is a directory **udev
writes** -- so on macOS the scan returned -1 always and **no board could
ever be listed**, through any of its six callers.  The Darwin half walks
`/dev` for the `cu.` nodes, and the two rules in it are the whole of it:
`cu` and not `tty`, because tty blocks until carrier is asserted and a
board that is only listening never asserts it; and named by what driver
claimed the device, not "everything unfamiliar", because the other way
round offers the Bluetooth port as a board and opening that starts a
pairing nobody asked for.  On this machine it correctly lists none.

And one the plan asked for that was already true: the desktop opens a
port on a worker (`probe_run`), not on the turn, so the thirty seconds a
CDC device that has not answered EP0 costs is thirty seconds of a
spinner and not of a frozen desktop.  Checked, not assumed.

**The screen.**  `brew install xorg-server` -- a formula, not the cask --
and the desktop came up on macOS: menu bar, Deskbar, clock, the home
window with its rows.  Three things followed from having a server to
ask.

*The scale.*  Reading `Xft.dpi` was already written and was never going
to fire: `Xft.dpi` is a thing a DESKTOP SESSION sets, and a Mac has no
session to set it -- the server carries no resource database at all.  So
the scale fell to the 96.0 default and a Retina screen drew everything
at half size.  The screen's own measurements are the fallback, and their
RATIO is right whichever convention a server reports them in.  Measured
on a 192-dpi server: the menu bar was 21 pixels and is 42.

*The command key.*  `event_modifiers()` had `ControlMask` written into
it.  Which bit carries Command is not a constant anywhere -- X names
eight modifiers by number and lets the keymap say what each one holds --
so it is asked of the server, and the seam says only WHICH KEY a machine
gives commands with.  Control still means command on both hosts, and on
a Mac the Command key does as well: added, not swapped, so nothing that
works stops working.  Decision 2 below is therefore still open and is
now only about whether Control should STOP.  Proving it turned up the
trap: Meta and Alt share a key on most keymaps, so the obvious rule
made Option mean command and Option-clicking a file would have acted on
it.  The four modifiers the desktop already spends are removed.

*The clipboard.*  `test-host-clipboard` and `test-host-render` had never
run on this machine -- they want a server and there was none -- and both
pass now, which is 11 assertions the port had never been under.  One
does not: Homebrew's `libX11` reports that it supports NO locale, not
even `C`, so `XOpenIM` cannot succeed and there is no input method at
all -- no Compose, no dead keys.  The host already falls back to
`XLookupString`, so plain keys are unaffected; the Compose assertions
now say they are skipping and why.  Whether XQuartz's own libX11 does
better is the one thing here still untested.

**The board, plugged in.**  An Apollo510 over USB, and milestone 6 is
finished: `tiku_sys_serial_ports()` lists exactly it and neither of the
two ports this machine always has; `tiku-desk-probe` reaches it in
eight tenths of a second, not the thirty a device that has not answered
EP0 costs; the desktop mounts it and the Deskbar shows its uptime, its
boot count and its free memory, with windows open on its `dev/` and
`mem/`.  Both board-marked scripts -- `devicerow` and `namespace` --
pass twice each, once offline and once against the hardware.

It did not work the first time, and what stopped it is worth keeping.

**A folder window could hang the desktop forever**, on any host, and a
Mac is where it finally did.  `tiku_image_is_image()` decides whether a
file is a picture by reading its first eight bytes, and it read them
from anything it could open.  A device answers the open and then never
answers the read; a pipe with nobody at the far end does not answer the
open at all.  Either stops the turn that asked, which is the whole
desktop, for as long as the thing stays quiet.  What found it was
`/dev/console`, which on a Mac belongs to whoever is logged in and is
theirs to read -- so the open succeeded and the read waited for
somebody to type.  Linux keeps that node to root, the open fails, and
the sniff is harmless, which is why it had never shown there.  A
picture is a regular file on every host, so that is asked first, of the
path rather than of the open file, because opening is itself the risk.

**And a board's path is not a host's path.**  The sniff that hung was
looking at the HOST's `/dev/console` because the BOARD has a node of
that name: run with `-d` the board's namespace is the root, so its
paths are board-absolute, and this one question took a board path
straight to `fopen()`.  The hang is gone -- a device is not a regular
file -- but the confusion under it is not: a board node whose path
matches a host file would still have that host file opened and its
first bytes read.  The harm is small (eight bytes, to answer one
question) and the fix is not: "does this node want a thumbnail" should
be answered by reading the node THROUGH ITS BACKEND, which is what
every other question about a node already does.  `tiku_thumb_wanted()`
takes a path where it wants a model.  Left as found and written down.

**And the suite is not hermetic while a board is plugged in.**  Two
lanes -- `repaint` and `slide` -- assert exactly how many rows a change
repaints, which is the whole point of them (PVL-075: a change to one
row paints one row).  With a board on the bus the desktop's own boot
discovery lists it, that lands a candidate, and the frame it causes is
one more paint than the script allows: `expect-drawn 3` sees 4.  Proved
rather than guessed -- with the scan made to return nothing, both pass;
with the board visible, both fail at every commit tried, including the
one that had scored 1430 of 1430 an hour before.

So `make test` currently depends on what is plugged into the machine,
which is the one thing a suite must not do.  The scripts that WANT a
board say so (`# board: yes`) and are given one through `TRK_PORT`; the
rest should be a closed world.  Three ways to make them one, and it is
a decision rather than a defect to be quietly patched: suppress boot
discovery when a script drives and no port was named; let the harness
point the seam at a directory it controls, the way it already controls
HOME and /tmp; or stop asserting exact paint counts, which throws away
what those two lanes exist to prove.  The first is the smallest and the
third is the worst.

### On the screen, at last

The done-when for milestone 6 said `./build/TikuDesktop` on a Retina
display at the right size, with the Command key working.  It is, as of
2026-09-17 on the iMac, through Homebrew's `xorg-server` rather than the
XQuartz cask: `X11.bin` and `quartz-wm` up on `:0`, "TikuOS Desktop"
2240x1126 viewable under the menu bar.  The server reports the screen
in LOGICAL pixels (2240x1230 over 593x325 mm, 95.9 dpi), so the scale
fallback answers 1 and macOS does the doubling -- which is the right
result, and not one that could have been guessed.  The live modifier
map carries Meta on Mod2 and nothing on Mod1, so the resolver yields
Control|Mod2: Command gives commands, Option does not.

What it took is in the README, because the formula's caveat omits all
three steps: `brew services` refuses xinit's plists (both are handed to
launchd by hand, the privileged one as root), and `bin/Xquartz` is a
stub that opens `libexec/X11.app` by bundle id -- LaunchServices has
never indexed a bundle inside the Cellar, so the stub exits 10 and
every client hangs on the socket until `lsregister -f`.  The bundle
launcher grew two guards on the way: a socket file outlives its agent,
and a loaded agent without the root helper still hangs clients, so a
socket counts only with both services present.

`-fullscreen` followed the same day: `tiku_host_set_fullscreen()` asks
the manager in the standard way (`_NET_WM_STATE_FULLSCREEN`, set before
the first map so the first frame is already the screen) and sizes the
window to the screen itself where there is no manager, so a bare Xvfb
answers too.  The size comes back as the resize the desktop already
handles.  Proved both ways: 2240x1230 at 0,0 under quartz-wm on the
iMac, 640x480 on the render test's private server.

### What four flaky tests actually were

`apphelp`, `helpreader`, `imageviewer` and `utilities` had been called
flaky three times without being looked at.  They were not flaky; they
were one bug, and it is the same shape as the autoscroll one above.

A scripted `wait` is counted in TURNS, and that is exactly right for
what it was written for -- a stepped copy, an armed rename, work this
process does a slice of per turn.  A child process starting is not
that.  A fork, an exec and a library loaded are work another process
does, which no number of turns here advances, and the scripted loop
does not sleep: a dozen turns can pass in less time than the fork
takes, and the window is simply not there when the next line asks for
it.  Whether it was there depended on how busy the machine happened to
be, which is why the failures moved around from run to run and why
running one lane alone usually "fixed" it.

So a turn spent waiting is now spent ON the child, when there is one
still coming up: `tiku_remote_wait()` waits on the descriptors and
returns the instant one of them has something.  The test is "still
coming up", not "still running" -- a child that has connected is one
more session, so a desktop with as many sessions as children has
nothing to wait for and goes back to counting turns.  A lane that
launches nothing never reaches any of it.

Four lanes, three runs each, 12 of 12 green where they had been 17
assertions short.  **This was never a macOS bug**: Linux races the same
way and usually wins.

### Milestone 7, which also needed no board

`make app` builds the bundle, and the one thing worth writing down is
the trap it hit.  A Mac's filesystem does not distinguish case, so a
binary copied in as `tikudesktop` and a launcher copied in as
`TikuDesktop` are ONE FILE, and the second copy wins.  What that
produced was a launcher exec'ing itself forever, out of a bundle that
looked correctly built and passed every check that did not run it.  The
binary is `TikuDesktop-bin` now, and the target asserts the two are
still two files before it signs.

### The Linux contract

What "unchanged" means, so it can be checked and not merely intended:

1. **No new `#ifdef` in the shells, the applications or a kit's logic.**
   The checker in milestone 2 holds it.  The only platform words in the
   tree are in the seam files it names.
2. **The Linux seam files are moves, not rewrites.**  `tiku_sys_linux.c`
   is the `/proc` readers as they stand; `tiku_volume_linux.c` is today's
   `tiku_volume.c`; the Pulse backend is untouched.  A reviewer diffs the
   function bodies against their old homes and sees whitespace.
3. **The Makefile picks by `uname -s`, nothing else.**  A Linux `make`
   compiles the same source list it compiles today plus nothing; the
   Darwin files are not in `KIT_SRCS` there, so a Linux link cannot even
   see them.
4. **`make test` on Linux proves what it proved.**  The harness changes
   in milestone 1 are helpers that reduce to today's commands on Linux
   (`second-volume` *is* `/dev/shm`; the resolved `/tmp` *is* `/tmp`).  No
   script loses an assertion; a script that must skip on one host says so
   on the other with a count.
5. **What changes on Linux changes on purpose, and is listed.**  From
   milestone 0: `tiku_draw_open()` zeroes its struct, and
   `test_device_utilities` stops reading freed memory.  From the four
   findings: `send_all()` and the client's mid-message read are bounded
   rather than endless (a fix, and the only one of these a shipped
   program can feel); `tiku_report.c` prints the faulting instruction;
   `test_remote` adopts the listener on a pair fed from the pty by a
   shovel thread, which is a test modelling two processes rather than
   one; and the drag target's autoscroll hold counts turns when a script
   drives it, which is a path only `-script` ever takes.  From milestone
   6, three that a Linux user sees and one they do not: the IDE stops
   listing the quoted source and the caret under a complaint as messages
   of their own, and a failed build's messages open on its first error
   rather than on nothing -- both are gcc's output read better, not
   clang's read at all -- and `tiku_conduct.c` now notices when a line
   did not take the rate it was given, where it had discarded the
   answer.  The one they do not see is `tiku_tx_scan_serial()` becoming
   a seam call whose Linux half is the `/dev/serial/by-id` walk moved
   whole, under rule 2.  Also from 6, and reaching Linux only where it
   was already broken: the UI scale falls back to the screen's own
   measurements when no session set `Xft.dpi`, which on a Linux desktop
   that sets it changes nothing; and `test_host_clipboard` skips its
   Compose assertions when Xlib says it supports no locale, which on
   Linux it never does say.  The command modifier is asked of the
   server rather than written down, but the seam answers "Control" on
   Linux, so the mask it computes there is the mask it had.  Nothing
   else on Linux is reached by any of it.

## 3. Decisions that are yours

1. **XQuartz or native.**  Milestone 6 makes the desktop usable on a Mac
   through XQuartz, which is what a developer who already has a Linux
   desktop expects; milestone 8 removes the dependency for a person who
   only has a Mac.  Eight is three to four days and a second window
   backend to keep true; the `tiku_host` seam was built for exactly this
   (the wasm build is the precedent), so the cost is the file, not a
   refactor.  The proposal: 6 now, 8 when someone who is not a developer
   is handed a Mac with this on it.
2. **Command or Control.**  *Half-answered by milestone 6, and the open
   half is smaller.*  On a Mac the Command key now gives commands, asked
   of the keymap rather than written down.  Control still does too, on
   both hosts, because adding is safe and swapping is not: nothing a
   person already knows stopped working.  What is left to decide is
   whether Control should STOP meaning command on a Mac -- which would
   free it for what a Mac uses it for, and would break every Control
   chord a developer moving between the two machines has in their
   hands.  The proposal: leave both, and revisit if someone trips.
3. **Is macOS a supported host or a development convenience.**  A
   development host needs 0 through 2 and 5: it builds, tests, and lets
   someone work on the tree from a Mac.  A supported host needs 3, 4, 6
   and a paragraph in the README that promises it.  The proposal: call it
   a development host after milestone 2 and decide about the rest when
   the first person who is not you runs it.
4. **Where the seam lives.**  `kits/support` for `tiku_sys` keeps it
   linkable by everything; `kits/device` would say "the machine" more
   plainly but the probe, which links support only, needs the self path.
   The proposal: support.

## 4. What this touches and what it does not

Touched: `mk/toolchain.mk` and `mk/kits.mk` (the per-host source list),
one new header and two new seam files per mechanism in `kits/support`,
`kits/storage` and `kits/media`, the five `/proc` callers reduced to seam
calls, `tiku_x11.c` for the scale and the modifier table, `tiku_tx.c`
for the rates, the IDE's message parser, `run.sh` and a dozen fixtures,
`check_architecture.py`, and `board/gui/tiku_draw.c` for the one line
that fixes a bug.

Not touched: the shells' logic, the pose view, the window controllers,
the display list, the fonts, the strings, the contract, the board side of
anything, and every Linux object that is not one of the two bug fixes.

Not in scope: the kernel and the boards (they never ran on the host),
the wasm build (its own backend, unaffected), and Windows (a different
plan, if ever; the seam this one builds would be its starting point).
