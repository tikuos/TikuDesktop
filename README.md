# applications — what runs on a TikuOS desktop, and the desktop itself

Host builds (Linux and macOS, X11) of the application tree: seven kits, the
Tracker rewrite with its two shells, and the applications that run on it.
This directory is its own repository, cloned into a tikuOS checkout at
`tikuOS/applications/`; see *Where it must live*, below.

## Source and binary ownership

| directory | holds | products, all under `build/` at this level |
|---|---|---|
| `kits/` | seven libraries a program links without knowing what else links them: `support`, `device`, `interface`, `storage`, `translation`, `application`, `media`.  `tools/check_kits.py` holds the dependency table and refuses an include that crosses it | `build/libtiku_*.a` and PIC twins under `build/pic/` |
| `TikuTracker/` | the file-manager core (`src/core`, `src/ui`, `src/deskbar`) **and both shells** (`src/shell`): the spatial desktop and the single-window browser are one product built two ways.  Also the unit tests, the 200-odd driven scripts, `spec/`, the strings catalogue and the icon sources | `build/system/TikuTracker`, `build/system/TikuDesktop`, their `build/dev/` flavours, `build/dev/trk-conduct`, every `build/tests/test_*` |
| `system/` | the application process host and desktop supervisor; no application UI | `build/system/tiku-run`, `build/system/tiku-desk` |
| `apps/` | app-owned code: Editor, Terminal, Basic/C, Bench, Console, Reader/Help, Image, Monitor, VFS Inspector, calculator, hex editor and sticky notes; examples and BASIC host | bundles in `build/apps/<id>/`; standalone samples in `build/examples/`, tools in `build/tools/` |
| `assets/` | the mark, the banners and splashes the applications wear | — |
| `tools/` | what works on the tree rather than in it: `split_file.py` cuts a file that grew section banners into a directory of one-functionality files, moving functions whole and generating the rest (`PLAN-split.md`) | — |

Each shell is one composition over the files of one directory:
`TikuTracker/src/shell/desktop/` for the spatial desktop and
`TikuTracker/src/shell/tracker/` for the single-window browser.  The
pose view, which both share, is `TikuTracker/src/ui/poseview/`.

Installed apps run in separate processes, including Reader/Help, Image, Monitor
and VFS Inspector. They use the public kits, never desktop-private headers.
See [application architecture](apps/ARCHITECTURE.md) for manifests, source and
binary boundaries, device services, migration and adding applications.

## Building

Everything builds from this directory, into one `build/`:

```sh
make -j$(nproc)          # kits, both shells in both flavours, the apps, every test
make test                # gates, unit suites, the BASIC host check, the scripts
make run                 # build/system/TikuDesktop, over the host filesystem
./build/system/TikuDesktop -d   # over a board on serial (-p PORT -b BAUD)
./build/system/TikuDesktop -fullscreen   # the window is the screen
```

The rules are `Makefile` and `mk/` (one fragment per tree); the Makefiles
inside `kits/`, `TikuTracker/` and `apps/` only forward here, so
`make test` typed in any of them is the same run.  One script alone:

```sh
TRK_SHELL_ONLY=about.script sh TikuTracker/tests/shell/run.sh
```

With a TikuOS board on a serial line, the scripts marked `# board: yes`
run a second time against it, mounted where the script expects the
mirrored files:

```sh
TRK_PORT=/dev/ttyACM0 make test
```

Read a result by its counts, not its colour: a suite that silently stopped
prints no failures.  `TikuTracker/HANDOVER.md` §2 lists the build's traps.

### On macOS

The same commands, with `sysctl -n hw.ncpu` where Linux says `nproc`,
and nothing to install beyond the command-line tools
(`xcode-select --install`).  **The desktop draws through the Mac's own
window** -- `kits/interface/tiku_cocoa.m`, one file behind the same
`tiku_host` seam the X11 host fulfils on Linux -- so there is no X
server to install, start, or attach to the login session:

```sh
make -j$(sysctl -n hw.ncpu)
make run                 # a window, Retina-sharp, with the Command key
make app                 # build/TikuDesktop.app, for the Dock
open build/TikuDesktop.app
```

`make app` writes `build/TikuDesktop.app`: the same binaries, the mark
rendered to an `.icns` at all ten sizes a Mac asks for, the applications
under `Contents/Resources`, and a launcher that tells the program where
they are.  It is signed ad hoc, so it opens on the machine that built
it.  After `make clean`, `make && make app` brings it back.

**No X server is needed to build or to test.**  The driven scripts run
with `DISPLAY=` empty and read the offscreen surface, so `make test` is
the same run here as there.

The X11 host is still there for a Mac that wants it: `make
TIKU_HOST=x11` builds the desktop over Xlib (the client library from
`brew install libx11`, or XQuartz's copy in `/opt/X11`, or
`X11_PREFIX=`), and `make TIKU_HOST=x11 test` also runs the two suites
that look at an X window from outside, `test-host-clipboard` and
`test-host-render`, on a private `Xvfb` from `brew install xorg-server`.
The bundle's launcher carries the choice (`Contents/Resources/host`)
and hunts for a launchd display socket only for an X build; how to put
such a server in front of a person is in `PLAN-macos.md`, with the
reasons it is no longer the way.

Two habits of the native window worth knowing.  Command and Control
both mean *command*, so the Control chords a Linux hand knows still
work and Cmd does what a Mac hand expects; Cmd-Tab belongs to the
system, so the desktop's own switcher is reached with Ctrl-Tab.  And
the desktop's quit is its own: the close box, the Dock's Quit and Cmd-Q
all ask the shell, which shuts down the way it does on Linux.

What differs underneath is said in two places and nowhere else: the
Darwin block in `mk/toolchain.mk`, and `kits/support/tiku_darwin.h`, which
is forced in front of every source so that none of them has to name a
platform.  `PLAN-macos.md` holds the rest -- what works, what is still
dark, and in which order it is being taken.

## Two flavours of each shell

`build/system/TikuDesktop` and `build/system/TikuTracker` are the product: they link a
stub that refuses `-script`.  `build/dev/…` link the script interpreter the
harness drives, and `tools/check_shipping.py` proves the vocabulary is
absent from what ships.  Run the scripts against `build/dev/`, hand a
person `build/system/` together with the `build/apps/` bundles.

## Where it must live

One thing pins the clone location: `apps/basichost` compiles the
board's own BASIC interpreter for the host from
`../../kernel/shell/basic/tiku_basic.c`, so this directory must sit at
`tikuOS/applications/`.  Everything else builds from anywhere.  The tikuOS
`.gitignore` covers `applications/`; commit here, not there.

## Reading order

`TikuTracker/HANDOVER.md` — how the work is done, the contract, what bites.
`TikuTracker/FEATURES.md` — the fidelity contract, 889 rows read from the
original source.  `TikuTracker/spec/` — the architecture as decided.
`apps/examples/README.md` — the least an application can be.
`PLAN-structure.md` — the shape this tree is being moved toward.
