# applications — what runs on a TikuOS desktop, and the desktop itself

Host builds (Linux and macOS, X11) of the application tree: six kits, the
Tracker rewrite with its two shells, and the applications that run on it.
This directory is its own repository, cloned into a tikuOS checkout at
`tikuOS/applications/`; see *Where it must live*, below.

## The four directories

| directory | holds | products, all under `build/` at this level |
|---|---|---|
| `kits/` | seven libraries a program links without knowing what else links them: `support`, `device`, `interface`, `storage`, `translation`, `application`, `media`.  `tools/check_kits.py` holds the dependency table and refuses an include that crosses it | `build/libtiku_*.a` and PIC twins under `build/pic/` |
| `TikuTracker/` | the file-manager core (`src/core`, `src/ui`, `src/deskbar`) **and both shells** (`src/shell`): the spatial desktop and the single-window browser are one product built two ways.  Also the unit tests, the 200-odd driven scripts, `spec/`, the strings catalogue and the icon sources | `build/TikuTracker`, `build/TikuDesktop`, their `build/dev/` flavours, `build/dev/trk-conduct`, every `build/tests/test_*` |
| `apps/` | what runs **on** the desktop: one directory per application (`edit/`, `term/`, `ide/`, `run/`), the toolkit `examples/`, the framework `samples/` (probe, gallery, demo), and the BASIC host | `build/tiku-*`, `build/apps/*.so` |
| `assets/` | the mark, the banners and splashes the applications wear | — |
| `tools/` | what works on the tree rather than in it: `split_file.py` cuts a file that grew section banners into a directory of one-functionality files, moving functions whole and generating the rest (`PLAN-split.md`) | — |

Each shell is one composition over the files of one directory:
`TikuTracker/src/shell/desktop/` for the spatial desktop and
`TikuTracker/src/shell/tracker/` for the single-window browser.  The
pose view, which both share, is `TikuTracker/src/ui/poseview/`.

## Building

Everything builds from this directory, into one `build/`:

```sh
make -j$(nproc)          # kits, both shells in both flavours, the apps, every test
make test                # gates, unit suites, the BASIC host check, the scripts
make run                 # build/TikuDesktop, over the host filesystem
./build/TikuDesktop -d   # over a board on serial (-p PORT -b BAUD)
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
the same run here as there.  XQuartz is for putting a window in front of
a person, and `make run` without one says `tracker: no display` and stops.

What differs underneath is said in two places and nowhere else: the
Darwin block in `mk/toolchain.mk`, and `kits/support/tiku_darwin.h`, which
is forced in front of every source so that none of them has to name a
platform.  `PLAN-macos.md` holds the rest -- what works, what is still
dark, and in which order it is being taken.

## Two flavours of each shell

`build/TikuDesktop` and `build/TikuTracker` are the product: they link a
stub that refuses `-script`.  `build/dev/…` link the script interpreter the
harness drives, and `tools/check_shipping.py` proves the vocabulary is
absent from what ships.  Run the scripts against `build/dev/`, hand a
person `build/`.

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
