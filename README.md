# applications — what runs on a TikuOS desktop, and the desktop itself

Host builds (Linux, X11) of the application tree: six kits, the Tracker
rewrite with its two shells, and the applications that run on it.  This
directory is its own repository, cloned into a tikuOS checkout at
`tikuOS/applications/`; see *Where it must live*, below.

## The four directories

| directory | holds | builds |
|---|---|---|
| `kits/` | six libraries a program links without knowing what else links them: `support`, `device`, `interface`, `storage`, `translation`, `application`.  `tools/check_kits.py` holds the dependency table and refuses an include that crosses it | `kits/build/libtiku_*.a` and PIC twins under `build/pic/` |
| `TikuTracker/` | the file-manager core (`src/core`, `src/ui`, `src/deskbar`) **and both shells** (`src/shell`): the spatial desktop and the single-window browser are one product built two ways.  Also the unit tests, the 200-odd driven scripts, `spec/`, the strings catalogue and the icon sources | `build/TikuTracker`, `build/TikuDesktop`, their `build/dev/` flavours, `build/dev/trk-conduct`, every `build/test_*` |
| `TikuDesktop/` | what runs **on** the desktop: the applications (`apps/`: edit, term, ide, run), the toolkit examples, the framework samples (`src/`: probe, gallery, demo), the BASIC host and the font tools.  Despite the name, the desktop *shell* is not built here — it is built next door and copied in by `make shell` | `build/tiku-*`, `build/apps/*.so`, and a copy of the shell |
| `assets/` | the mark, the banners and splashes the applications wear | — |

The desktop shell's source is `TikuTracker/src/shell/tiku_trk_desktop.c`;
the browser shell's is `TikuTracker/src/shell/tiku_trk_app.c`.

## Building

```sh
cd TikuDesktop && make all -j$(nproc)   # kits, then the apps, then the shell from next door
./build/TikuDesktop                     # the desktop, over the host filesystem
./build/TikuDesktop -d                  # over a board on serial (-p PORT -b BAUD)
```

`make all`, not bare `make`: the first rule in that Makefile is the BASIC
host, so a bare `make` builds only `build/tiku-basic` and stops.

The full test run lives with the shells:

```sh
cd TikuTracker && make test             # gates, unit suites, the BASIC host check, the scripts
TRK_SHELL_ONLY=about.script sh tests/shell/run.sh   # one script
```

Read a result by its counts, not its colour: a suite that silently stopped
prints no failures.  `HANDOVER.md` §2 lists the build's traps.

## Two flavours of each shell

`build/TikuDesktop` and `build/TikuTracker` are the product: they link a
stub that refuses `-script`.  `build/dev/…` link the script interpreter the
harness drives, and `tools/check_shipping.py` proves the vocabulary is
absent from what ships.  Run the scripts against `build/dev/`, hand a
person `build/`.

## Where it must live

One thing pins the clone location: `TikuDesktop/basichost` compiles the
board's own BASIC interpreter for the host from
`../../kernel/shell/basic/tiku_basic.c`, so this directory must sit at
`tikuOS/applications/`.  Everything else builds from anywhere.  The tikuOS
`.gitignore` covers `applications/`; commit here, not there.

## Reading order

`TikuTracker/HANDOVER.md` — how the work is done, the contract, what bites.
`TikuTracker/FEATURES.md` — the fidelity contract, 889 rows read from the
original source.  `TikuTracker/spec/` — the architecture as decided.
`TikuDesktop/examples/README.md` — the least an application can be.
`PLAN-structure.md` — the shape this tree is being moved toward.
