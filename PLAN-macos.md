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
| **2** | The system seam | `kits/support/tiku_sys.h`: `tiku_sys_processes(fn, ctx)` (pid, name, command line, uid), `tiku_sys_cpu_count()`, `tiku_sys_memory(total, free)`, `tiku_sys_load()`, `tiku_sys_uptime()`, `tiku_sys_self_path(buf)`, `tiku_sys_symbolise(addr, buf)`.  The header and its two files already exist, with `tiku_sys_fault_pc()` in them and the Makefile choosing by `uname -s`: this milestone adds functions to a seam rather than inventing one.  `tiku_sys_linux.c` is the `/proc` code lifted verbatim from the deskbar roster, the About window, `tiku_trk_task.c`, `tiku_demo_app.c` and the Restart item; `tiku_sys_darwin.c` is `proc_listpids`/`proc_pidpath`, `sysctlbyname` (`hw.ncpu`, `hw.memsize`, `vm.page_free_count`, `kern.boottime`), `getloadavg()`, `_NSGetExecutablePath()`, `atos -o`.  `tiku_report.c` takes the faulting PC from the signal's own context and prints it above the frame walk: a function that only stores keeps no frame pointer on arm64, so the walk steps straight past the one frame a reader most wants (found here -- `the_place_it_happened` shows as `main + 1716`).  The five callers call the seam.  `check_architecture.py` gains the rule: `/proc/`, `__linux__` and `__APPLE__` are allowed only in the kits' seam files -- `tiku_sys_*.c`, `tiku_darwin.h`, `tiku_volume_*.c`, `tiku_sound_*.c`, and the four that carry a binary format or a socket option rather than a mechanism (`tiku_app.c`, `tiku_app_load.c`, `tiku_client.c` for the stamp's section name, `tiku_backend_posix.c` for reading it, `tiku_link_tcp.c` for SO_NOSIGPIPE) -- and nowhere under `TikuTracker/src`, `apps/` or `board/`, which name no platform today and must not start | one to two days | the Deskbar lists running programs on both hosts; About says the CPU count, memory and load on both; Restart restarts on both; the checker passes and a planted `/proc` in a shell fails it; `test_report` and `crash.script` name the faulting function on both hosts |
| **3** | Sound | `kits/media/tiku_sound_coreaudio.c`: an `AudioQueue` of three 4 KB buffers fed from the same PCM ring the Pulse backend drains, opened on first play and closed after two seconds idle, linked with `-framework AudioToolbox` on Darwin only (the Makefile picks the backend file by `UNAME_S`; the Pulse file is not compiled there, and the CoreAudio file is not compiled on Linux); the same silence-with-one-line rule when the device refuses | one day | the startup chime and the beep from the Mac's speaker; `sounds.script` green on both hosts; the Linux build's `libtiku_media.a` has the same members it has today |
| **4** | Disks and the way out | `tiku_volume_darwin.c` grows: partitions from DiskArbitration (`DASessionCreate`, `DADiskCopyDescription` for whole disks and volumes not yet mounted, replacing `/proc/partitions`), mount and eject through the same framework rather than `mount(2)`, so the "Mount" and "Eject" items do what Finder does; the removable rule reads `DAVolumeRemovable`/`kDADiskDescriptionDeviceInternalKey` rather than guessing from the path.  Force Quit: `tiku_remote_process_handle` on Darwin returns a pid pinned by `proc_pidinfo`'s start time (`PROC_PIDTBSDINFO.pbi_start_tvsec`), and `_kill` re-checks the start time before `kill(2)`, which is the pidfd guarantee -- the process is the one that was seen -- by other means | one to two days | a USB stick appears on the desktop when plugged in and leaves when ejected from its menu; `forcequit.script` green on both; a pid reused between the dialog and the kill is not killed |
| **5** | Search that ends | Platform-independent, found here: a query whose scope is a volume walks the whole boot disk, and `is_tiku_app()` opens every file the walk lists to read its header.  Scope: the default scope of a new query is the folder it was opened from, "this volume" is a choice; the walk skips `/System`, `/Library`, `/private/var`, `/proc`, `/sys`, `/dev` and any mount that is not local; a query yields to the turn every N rows as the pose view already does; `is_tiku_app()` is asked only of files with an execute bit and its answer cached by `(dev, ino, mtime)` in the model.  `query.script` and `qtemplate.script` gain a wall-clock ceiling | one day | both query scripts finish in under ten seconds on this machine with a full home directory; a listing of `/usr/bin` costs no more `open(2)` calls than there are executables in it |
| **6** | A person at the desk | With XQuartz: `tiku_x11.c` reads the Retina scale from `Xft.dpi`/the screen's millimetres so the desktop is not drawn at half size; Command mapped to the desktop's command modifier where Control is today, from a table in one place (`tiku_event.h`'s modifier names), so the Linux keys stay as they are; the X11 clipboard proved against XQuartz's pasteboard bridge; the serial rates above 230400 set with `IOSSIOSPEED` when `tcsetattr` refuses them (`tiku_tx.c`, behind the seam); the IDE's message parser reads clang's diagnostic lines (`file:line:col: error:` is shared; the "note:" and caret lines differ) and `idebuild.script` runs with `CC=clang` on Linux too, so the parser is proved on both; `tiku-desk-probe` and the board link tried against a real board on a Mac, where a CDC device that has not answered EP0 blocks the open for thirty seconds and the desktop must not wait on it in the turn (open the port off the turn, as the utility jobs already are) | two days | `./build/TikuDesktop` on a Retina display at the right size, Cmd-key menus working, paste from another Mac application, a board connected over USB with its panel live; the Linux desktop's keys, clipboard and serial path unchanged, proved by the scripts |
| **7** | The bundle | *Optional.*  `make app` writes `build/TikuDesktop.app` (Info.plist, the mark as `.icns` rendered from the same drawing, the binary and `apps/` inside `Contents/Resources`, `TIKU_APPS` set by the launcher); `LSUIElement` off so it has a Dock tile; codesign ad hoc so Gatekeeper opens it on the machine that built it | half a day | double-clicking the bundle opens the desktop with its applications found |
| **8** | Native, without XQuartz | *A decision, not a default (§3).*  `kits/interface/tiku_cocoa.m`: the same `tiku_host` contract `tiku_x11.c` fulfils today and the wasm build replaces -- one window, a backing `CGImage` blitted per present, events translated to `tiku_event_t`, the pasteboard for the clipboard, `NSTextInputClient` for the input method; chosen by `TIKU_HOST=cocoa` in the Makefile, X11 remaining the default on both hosts until it is proved; the scripts do not know which host backend drew | three to four days | every script that passes under X11 passes under Cocoa; a Mac with no XQuartz runs the desktop |

Order: 0 then 1 (nothing else can be proved until the harness tells the
truth on both hosts), then 2, 3, 4 in any order, 5 whenever (it is not a
macOS milestone but macOS is where it bites), 6 when a person needs it,
7 and 8 on decision.  Six to eight days for 0 through 6.

### Where it stands (2026-09-14)

Milestones 0 and 1 are built.  On this machine -- Apple Silicon, the
command-line tools, Homebrew's `libx11`, no X server:

| | before | after 0 and 1 |
|---|---|---|
| the build | 0 errors | 0 errors, ten seconds from clean |
| the gates | all green | all green |
| the unit suites | 125 of 134 | **133 of 134** |
| the driven scripts | 17 of 265 runs failed | **6 of 264 failed, 4 skipped out loud** |
| `supervisor.sh` | 16 of 18 | **18 of 18** |

What is left is waiting on a milestone, not on a mistake:

- `test_forcequit`, `forcequit.script` -- no pidfd (milestone 4).
- `idebuild.script`, `idec.script` -- the IDE reads gcc's diagnostics,
  not clang's (milestone 6).
- `query.script`, `qtemplate.script` -- a search walks the whole boot
  volume and does not come back (milestone 5).  Both had to be killed,
  and killing a desktop leaves its socket behind, which is why
  `remoteapp.script` two rows later fails in a full run and passes on
  its own.  Milestone 5 takes all three with it.

And four say why they did not run: `dropfont` (no DejaVu Sans to drop),
`panelroot` (it counts the steps from the fixture up to the root, and
`/private/tmp` is one deeper), `unmount` and `sounds` (both want a volume
this user may NOT unmount; the ram disk the harness mounts is its own).

Four things the work turned up that are nobody's platform.  All four are
fixed; two of them were not what they first looked like.

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
4. **`scroll.script` was not what the flake looked like.**  The autoscroll
   hold was said to be counted per draw and the scripted `wait` per turn;
   in fact both are per turn, and the band controller was never the one
   scrolling -- its folder is NULL throughout.  The drag TARGET does the
   autoscroll, and its hold was still `now - scroll_at <= 600 ms`, wall
   time, while its spring beside it had already been taught to count
   pulses.  So whether a script's twenty-five turns had used up six
   hundred milliseconds was the machine's load, run to run.  The
   autoscroll now counts turns under a script, exactly as the spring and
   the band do, and the script passes every time.

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
   drives it, which is a path only `-script` ever takes.  Nothing else
   on Linux is reached by any of it.

## 3. Decisions that are yours

1. **XQuartz or native.**  Milestone 6 makes the desktop usable on a Mac
   through XQuartz, which is what a developer who already has a Linux
   desktop expects; milestone 8 removes the dependency for a person who
   only has a Mac.  Eight is three to four days and a second window
   backend to keep true; the `tiku_host` seam was built for exactly this
   (the wasm build is the precedent), so the cost is the file, not a
   refactor.  The proposal: 6 now, 8 when someone who is not a developer
   is handed a Mac with this on it.
2. **Command or Control.**  The desktop's shortcuts are Control today
   because X11 on Linux is.  A Mac user's hand goes to Command.  The table
   in milestone 6 maps one to the other per host; the alternative is to
   leave Control everywhere, which is honest and slightly wrong on a Mac.
   The proposal: Command on macOS, in one table.
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
