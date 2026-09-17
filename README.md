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
| `apps/` | app-owned code: Editor, Terminal, Basic/C, Bench, Console, Reader/Help, Image, Monitor, VFS Inspector, calculator and hex editor; examples and BASIC host | bundles in `build/apps/<id>/`; standalone samples in `build/examples/`, tools in `build/tools/` |
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

The same commands, with `sysctl -n hw.ncpu` where Linux says `nproc`.
What has to be there is the command-line tools (`xcode-select --install`)
and the X11 client library, which Homebrew has as `brew install libx11`;
`mk/toolchain.mk` finds it, or XQuartz's copy in `/opt/X11`, or takes
`X11_PREFIX=` from you.

**No X server is needed to build or to test.**  The driven scripts run
with `DISPLAY=` empty and read the offscreen surface, so `make test` is
the same run here as there.

Two suites do want one, and `make test` runs them when it finds it:
`test-host-clipboard` and `test-host-render`, which drive a real server
on a private display.  `brew install xorg-server` gets you one -- it is
a FORMULA, so it wants no password, unlike the XQuartz cask -- and puts
`Xvfb` on the path, which is what those two look for.

`make app` writes `build/TikuDesktop.app`: the same binaries, the mark
rendered to an `.icns` at all ten sizes a Mac asks for, the applications
under `Contents/Resources`, and a launcher that tells the program where
they are.  It is signed ad hoc, so it opens on the machine that built
it.  Double-clicking it still needs a server -- the launcher finds
XQuartz's socket if XQuartz is running, and says so in a dialog if
nothing is, rather than bouncing once in the Dock and dying quietly.

For a window in front of a person, `Xvfb` is not enough: the server
has to be attached to the login session.  The XQuartz cask does that;
so does the same `xorg-server`, with three steps its caveat does not
say (the second needs a password, once):

```sh
brew install xinit
cp /opt/homebrew/opt/xinit/homebrew.mxcl.startx.plist ~/Library/LaunchAgents/   # loads at every login
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/homebrew.mxcl.startx.plist  # and now
sudo cp /opt/homebrew/opt/xinit/homebrew.mxcl.privileged_startx.plist /Library/LaunchDaemons/ \
  && sudo chown root:wheel /Library/LaunchDaemons/homebrew.mxcl.privileged_startx.plist \
  && sudo launchctl bootstrap system /Library/LaunchDaemons/homebrew.mxcl.privileged_startx.plist
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
  -f /opt/homebrew/opt/xorg-server/libexec/X11.app
open build/TikuDesktop.app
```

`brew services` refuses both plists ("no locatable service file"), so
they are handed to `launchd` by hand; and `bin/Xquartz` is only a stub
that opens `X11.app` by bundle id, which LaunchServices has never seen
inside the Cellar until it is registered -- without that the stub exits
10 and every client that connects hangs.  The bundle's launcher finds
the resulting socket on its own.  Two habits of that server: it exits
when its last client leaves, and a fresh connection while one is still
up re-runs `startx`'s privileged `10-tmpdirs`, which recreates
`/tmp/.X11-unix` and deletes the live socket -- after which every client
says `no display` though `X11.bin` is running.  `launchctl kickstart -k
gui/$(id -u)/homebrew.mxcl.startx` starts it over.  `make run` without
any server says
`tracker: no display` and stops.  One thing to know: Homebrew's
`libx11` reports that it supports no locale at all, not even `C`, so
`XOpenIM` cannot succeed and there is no Compose, no dead keys and no
input method on that build.  Plain keys are unaffected -- the host falls
back to `XLookupString` -- and the Compose assertions say they are
skipping and why.  Whether XQuartz's own copy of libX11 in `/opt/X11`
does better is untested.

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
