# The applications tree: one build, and names that say what they hold

> **Landed 2026-09-03/04**, Phases 0–6 and the follow-ups: the root build
> (`a4467e6`..`a30fff3`), the rename (`fc07933`), the shell in fourteen files
> (`24706a9`..`bec9f20`, `2e7dbcc`), the harness (`3e831cc`, `954299b`),
> the generated files (`f6bb2fb`, `5cc6e73`), then the boot in stages, the
> loop as a pump between two turn functions, headers for the simplest
> setups, and `make lint`.  Where this plan and the tree differ, the tree is
> right; the differences are recorded in the sections they belong to.  What
> remains is Phase 6, the device host, which waits on the device work.

A plan for the shape of `applications/` — where sources live, how the
products are built, and how the tests are found — written for an agent with
no prior context.  Every number below was measured against the tree on
2026-09-03 (`fb62222`, with the About-box and shutdown-screen work
uncommitted on top); nothing here is asserted from memory.

The tree is not in bad shape.  The six kits are the best-structured part of
the whole TikuOS project, and the test surface — 74 unit binaries, 203
driven scripts, and three build-time checkers — is a real asset.  What has
gone wrong is what goes wrong in any tree that grows by 258 commits in
three weeks: the build never caught up with the structure, and one
directory is named for a product it does not build.

---

## 0. What the tree is, in numbers

| Directory | Tracked files | What it holds |
|---|---:|---|
| `kits/` | 114 | six archives: support, device, interface, storage, translation, application; `tools/check_kits.py` enforces the dependency table |
| `TikuTracker/` | 490 | the file-manager core (`src/core`, `ui`, `deskbar`) AND both shells (`src/shell`), 74 unit tests, 203 scripts, 17 spec files, the strings catalogue, the icon sources |
| `TikuDesktop/` | 35 | four applications (edit, term, ide, run), five examples, the BASIC host, three framework samples (probe, gallery, demo), the font tools |
| `assets/` | 7 | banners, splashes, the mark, one reference screenshot |

Source volume: `TikuTracker/src` is ~57 K lines; `kits/` is ~28 K lines
plus the 163,569-line baked font header; `TikuDesktop` is ~8 K lines.  The
largest single things: `src/shell/tiku_trk_desktop.c` at 9,682 lines and
142 functions, of which `tracker_run()` is 3,879; `src/ui/tiku_trk_poseview.c`
at 6,529.

Build machinery: three Makefiles (`kits` 138 lines, `TikuTracker` 644,
`TikuDesktop` 254) and `tests/shell/run.sh` at 858 lines.

---

## 1. Findings

Each is stated with the evidence and with what it costs.  They are ordered
by how much they cost, not by how hard they are to fix.

### A. No object files exist.  Every binary recompiles its sources.

There is not one `.o` rule in `TikuTracker/Makefile`.  Every test and both
shells in both flavours are linked straight from `.c`:
`$(CC) $(CFLAGS) -o $@ $^`.  `tiku_trk_poseview.c` is compiled from source
by nine different binaries; the shared core by every one of the 74 tests
that links it, and four more times for the four shell binaries.

Measured: `touch src/core/tiku_trk_task.c && make all -j12` takes
**23.8 s wall, 148 s CPU** on twelve cores.  A clean build takes about the
same.  Incremental and clean are the same build, which means every edit
during a session is paid for at clean-build price, and `make test` — which
depends on `all` — never starts sooner than that.

### B. The two application Makefiles call each other.

`TikuTracker/Makefile` `desk-apps` runs `make -C ../TikuDesktop`;
`TikuDesktop/Makefile` `shell` runs `make -C ../TikuTracker`; both run
`make -C ../kits`.  The Makefile itself says why `desk-apps` names its
targets rather than asking for `all`: *"the two trees calling each other's
all is a loop."*  There is no entry point above them.  `make` in
`applications/` fails with *No rule to make target*, which is the first
thing a person types.

The `libs`-first ordering in `TikuDesktop/Makefile` exists because two
sub-makes would otherwise write the same archives at once under `-j`.
That is a symptom: recursion turns one dependency graph into three that
cannot see each other.

### C. The directory named TikuDesktop does not build TikuDesktop.

The desktop shell is `TikuTracker/src/shell/tiku_trk_desktop.c`, built as
`TikuTracker/build/TikuDesktop`, and *copied* into `TikuDesktop/build/` by
`make shell`.  `TikuDesktop/Makefile` opens with a paragraph explaining
this.  What `TikuDesktop/` actually holds is the applications and samples
that run *on* the desktop.  The runtime shares the confusion: the shell
locates its loadable apps by climbing from its own binary looking for a
directory literally named `TikuDesktop/build/apps`
(`find_apps_dir()`), so the source directory's name is baked into where
the product looks at run time.

### D. Every test lists its sources by hand, and the list of tests is kept twice.

Seventy-four rules of the form
`$(BUILD)/test_X: src/a.c src/b.c tests/test_X.c` — HANDOVER.md's first
build trap: *"Adding a dependency to a shared file breaks six targets at
link time."*  The set of tests to build (`TESTBINS`) and the set to run
(the `test:` recipe, 76 lines of `@$(BUILD)/test_…`) are two lists; the
Makefile's own comment records that they drifted, and HANDOVER records
`test_launch` sitting built-but-never-run for a session.  The last nine
lines of the recipe are tests found that way.

### E. The desktop shell is one 9,682-line file with a 3,879-line function.

`tracker_run()` is the argument parser, the boot sequence, the whole
event dispatch (keyboard, pointer, Deskbar picks, drops), the frame loop,
and the teardown.  The file already carries five banner comments naming
its parts — the embedded descriptor host, remote sessions, the global
menu bar, the Open/Save panel, the conductor channel — which is a file
listing the modules it should be.  Both of today's fixes (the About box's
tab, the closing screen) had to be threaded through it.

### F. The script harness keeps per-script setup in a 58-case `case` block.

Scripts declare what they need in headers — `# shell:`, `# home: fixture`,
`# apps: fixture`, `# splash: on`, `# argv: none`, `# work: home` — and
`run.sh` reads them.  Everything that did not fit a header went into a
`case $(basename "$s") in …` block of 58 labels: which scripts get a
`Documents/` folder, a query file, a background image, a second mount.
The setup for `deskprefs.script` is a Python program embedded in the shell
script, 500 lines from the script it serves.

A second gap, found today: a script that stops early — because the shell
exited under it — is reported green.  `tiku_trk_script_report()` refuses
only a script that asserted *nothing*; two checks of four is *2 passed,
0 failed*.  The shutdown-screen test would have passed with the feature
absent.

### G. Generated headers are produced in one directory and tracked in another.

`TikuDesktop/tools/genfont.py` writes `kits/interface/tiku_font_data.h`
(163 K lines, tracked).  `TikuTracker/tools/genstrings.py` writes
`kits/support/tiku_strings_default.h` from `TikuTracker/strings/en.xml`.
The kits cannot regenerate their own inputs: their generators live in the
applications above them, which is the dependency direction
`check_kits.py` exists to forbid, one level up from where it looks.

### H. One reach into the OS repository, undocumented at this level.

The only include in the tree that crosses a directory boundary is
`TikuDesktop/basichost/tiku_basic_host.c` →
`../../../kernel/shell/basic/tiku_basic.h`, and its Makefile rule compiles
`../../kernel/shell/basic/tiku_basic.c` with fourteen `-D` gates behind a
shim tree (`basichost/{tiku.h,hal/,kernel/}`) that imitates the OS include
layout.  It is the one fact that fixes where this repository must be cloned,
and nothing at the root says so.

### I. Duplicated and stray

- Four `.gitignore` files; the root's is a copy of `TikuTracker`'s.
- `CFLAGS`, `-std=gnu11 -D_DEFAULT_SOURCE`, and the six `-I` kit roots
  are written out in three Makefiles.
- `sweep-legacy` exists twice, for two renames.
- `DESK_SOURCES := $(shell find $(KITS) …)` runs a `find` on every `make`.
- `TikuDesktop/tests/test_app.c` is run by `make test` in `TikuDesktop`,
  which nothing calls.
- `applications.mk` at the root is untracked, references nothing that
  exists, and is the OS Makefile's `-include` hook.
- `assets/reference/photoshop7.png` — a reference screenshot in git (68 KB;
  an earlier draft of this list said 2 MB, which was the whole `assets/`
  directory, mostly the banners).
- `tools/glyphdump.c.gamma`.
- `_attic-2026-09-03/` — 31 MB of pre-restructure leftovers parked there
  during today's clone reshuffle; delete once you have looked.
- No `README.md` at the root: a fresh reader lands in four directories with
  no map.  HANDOVER.md carries a path to the home machine
  (`/home/ambujv/Tiku/tikuOS`) and says there is no `make lint` here.

### J. What is already right, and must be kept

- **The kits and `check_kits.py`.**  A dependency table with a checker is
  the single best structural decision in the tree.
- **`check_architecture.py` asking make** (`print-ownership`) which files
  belong to which shell, rather than re-parsing the Makefile.
- **Product/dev flavours** and `check_shipping.py` proving the script
  interpreter is absent from what ships.
- **The host seam.**  `tiku_host.h` is nine functions; `tiku_x11.c` is the
  only file in the tree that includes Xlib.  A device display is one file
  away.
- **The script grammar**, the `check(cond, "ROW", "what it proves")` idiom,
  the mutation discipline in HANDOVER §3, and the strings catalogue with
  its runtime override.

---

## 2. The target

### 2.1 Layout

```
applications/
├── Makefile              the ONE entry: make, make test, make run, make clean
├── mk/
│   ├── toolchain.mk      CC, CFLAGS, warning set, include roots — written once
│   ├── kits.mk           the six archives and their PIC twins
│   ├── tracker.mk        the core objects, both shells, both flavours
│   ├── apps.mk           standalone + .so from one source, per application
│   └── tests.mk          one pattern rule; the test list is the directory
├── README.md             the map: what is where, how to build, how to run
├── .gitignore            one
├── kits/                 unchanged
├── TikuTracker/          the file-manager core and BOTH shells (unchanged name)
│   ├── src/core ui deskbar
│   ├── src/shell/        desktop/  browser/  common/   (see 2.3)
│   ├── spec/ strings/ tools/ tests/
├── apps/                 ← today's TikuDesktop/: what runs ON the desktop
│   ├── edit/ term/ ide/  one directory per application (source + its .so table)
│   ├── run/
│   ├── examples/
│   ├── samples/          probe, gallery, demo (today's TikuDesktop/src)
│   ├── basichost/
│   └── tools/            genfont moves to kits/interface/tools (2.5)
├── assets/
└── build/                ONE tree, gitignored
    ├── kits/…            objects and libtiku_*.a, pic/
    ├── tracker/…         core objects, compiled once
    ├── TikuDesktop  TikuTracker           the product
    ├── dev/TikuDesktop dev/TikuTracker dev/trk-conduct
    ├── apps/*.so  apps/programming/*.so   what the desktop offers
    ├── tiku-edit tiku-term tiku-run tiku-basic tiku-example-*
    └── tests/test_*
```

The rename is one `git mv TikuDesktop apps` and it removes finding C
outright: no directory is named for a product, the product names appear
only on binaries, and `build/TikuDesktop` at the root is where a person
expects the desktop to be.  `TikuTracker/` keeps its name; it is the
Tracker rewrite, of which the desktop shell is one front-end, exactly as
HANDOVER §1 describes.  The runtime search in `find_apps_dir()` becomes
`<root>/build/apps` — one level, no product name in the path — with
`TIKU_APPS` still overriding for an installed layout.

### 2.2 The build

One non-recursive GNU make.  The root `Makefile` sets the toolchain and
includes the four fragments; every rule writes under `build/`; there is one
dependency graph, so `-j` is correct without ordering hacks and the
`libs`-first dance goes away with the recursion that needed it.

**Objects, once.**  `build/tracker/%.o: TikuTracker/src/%.c` with `-MMD`,
the same as `kits/Makefile` already does.  The shared core becomes an
archive, `build/tracker/libtracker.a`, that both shells and every test link;
the linker pulls what is referenced.  The desktop shell's own sources and
the browser shell's own sources are the two short lists that remain, and
`print-ownership` prints them as it does today so `check_architecture.py`
does not change.

**Tests are the directory.**  `TESTS := $(wildcard TikuTracker/tests/test_*.c)`;
one pattern rule links each test from its object, the objects of the
sources its table line names (`test_alert_SRCS := …`), and the six kits;
`make test` runs that same list, so a test cannot be built and not run.
Every test already linked all six kits — the shared prerequisite line
`$(TESTBINS) …: $(DESK_LIBS)` put them in `$^` — so no test was ever the
libc-only proof its bare rule suggested; the static guard for headless
modules is `check_architecture.py`'s include check, and it stays.  What
goes is the seventy copies of the same recipe.

**Applications from one source.**  Each `apps/<name>/` has one `.c` and
`apps.mk` builds two things from it: `build/tiku-<name>` and
`build/apps/<label>.so` (`-DTIKU_APP_EMBED -DTIKU_APP_SO` against the PIC
kits).  The label table — `edit→editor`, `term→terminal`,
`ide→programming/TikuBasic` and `programming/TikuC` with `-DTIKU_IDE_C` —
is five lines in one place instead of five near-identical rules.

**Kept as they are:** the kit archive rules (already right, including the
`rm -f` before `ar`), `sweep-legacy` (once), `.DEFAULT_GOAL := all`, the
product/dev split, `check-architecture`, `check-shipping`, `check_contract`,
`check_menu_commands`, and the BASIC host's transcript check.

**Entry points at the root:**

```
make            everything: kits, both shells in both flavours, apps, tests
make test       check-* gates, unit tests, the BASIC host check, the scripts
make run        build/TikuDesktop
make apps       the .so tree and standalone apps only
make clean
make -C TikuTracker …   gone; a thin forwarding Makefile per directory is
                        optional (`%: ; $(MAKE) -C .. $@`) if the habit is
                        worth keeping
```

Acceptance for this phase is arithmetic: the same 74 unit binaries, the
same pass counts (2,808 unit checks across them, 203 scripts), `check_shipping` still
finding no script vocabulary in `build/TikuDesktop`, and the incremental
rebuild after touching one core file measured at under three seconds
instead of 23.8.

### 2.3 The desktop shell in files

`src/shell/tiku_trk_desktop.c` becomes `src/shell/desktop/`, split along
the banners it already carries, one commit per extraction, suite green
after each:

| file | today's lines | what it is |
|---|---|---|
| `tiku_trk_desktop.c` | — | `tracker_run` reduced to argv, the boot sequence, the frame loop, and teardown |
| `tiku_trk_desktop_events.c` | most of 6,600–9,600 | the event switch: keys, pointer, Deskbar picks, drops — the body of the 3,879-line function |
| `tiku_trk_desktop_embed.c` | 1,248–1,780 | the embedded descriptor host: the services table over the workspace |
| `tiku_trk_desktop_remote.c` | 1,780–2,081 | remote sessions over the desk socket |
| `tiku_trk_desktop_globalbar.c` | 2,081–3,588 | the global menu bar and the publish protocol |
| `tiku_trk_desktop_panel.c` | 3,588–5,448 | the Open/Save panel's shell glue |
| `tiku_trk_desktop_conduct.c` | 5,448–5,700 | the conductor channel |
| `tiku_trk_desktop_lifecycle.c` | — | `splash_stage`, `begin_shutdown`, the teardown stages |
| `tiku_trk_desktop_private.h` | — | `desktop_t` and the statics the parts share |

`desktop_t` is already one struct passed by pointer, which is what makes
this mechanical.  *As landed:* fourteen files, split by function cluster
rather than by line range, since the banners marked where a topic began
but not where it ended; the boot is nine stage functions and the loop a
pump between `desktop_turn_begin` and `desktop_turn_end`; the largest
function in the directory is 476 lines.  `print-ownership` keeps the sources in `DESKTOP_SRC`, so
the ownership check sees the split as the same binary it always was.
`tiku_trk_poseview.c` at 6,529 lines is the next candidate but is out of
scope here: the spec's extraction order already treats it as one engine.

### 2.4 The harness

Two changes, both small.

**Headers replace the case block.**  The 58 cases fall into six kinds of
setup: extra folders, a file with given contents, a settings line, a
background image, a second mount, a strings directory.  Each becomes a
header the script carries itself:

```
# fixture: Documents/ recipes.query="q"
# settings: GlobalMenuBar on
# mount: /tmp/trk_mount
# lang: fr
```

`run.sh` shrinks to the loop, the header reader, and the conducted lane.
The one setup that is a program — the two-colour background for
`deskprefs.script` — becomes `tests/shell/fixtures/background.py`, called
by name.  Whether the loop itself is then worth rewriting as
`run.py` (it is already a Python-tooling tree: twelve tools) is a
decision, below.

*As landed:* every case body became `tests/shell/setup/<name>.sh` verbatim
first, then the fourteen that were only mkdir/echo/touch/rm/ln became the
headers above, each proven to lay the same tree as the file it replaced;
forty-two that plant programs, pictures or projects remain files, which the
`# setup:` header can also name.

**A script that stops early fails.**  `tiku_trk_script_report()` knows the
cursor and the op count; a script the shell left with steps unreached
reports `stopped at line N, step K of M, steps unreached` and counts as a
failure.  *As landed:* eighteen scripts have no `quit` and end by running
off their last step, which the first draft of the rule called a failure;
the rule is unreached steps, not a missing `quit`.

### 2.5 Generated files

A kit owns what it needs to rebuild itself.

- `genfont.py` → `kits/interface/tools/genfont.py`.  The baked header stays
  tracked (regenerating needs PIL and the DejaVu face; a build should not).
  A `check-font` target compares the header's stamp against the generator
  and the face, so a stale bake is caught rather than shipped.
- `genstrings.py` → `kits/support/tools/genstrings.py`.  The baked catalogue
  is generated *at build time* into `build/gen/tiku_strings_default.h`
  (python3 stdlib only) and stops being tracked.  `strings/en.xml` stays
  with the shells whose words it holds; the kit provides the mechanism and
  is handed the catalogue by the build.

### 2.6 The OS dependency

`TIKUOS ?= $(abspath ../..)` in the root Makefile, and the BASIC host is
built only when `$(TIKUOS)/kernel/shell/basic/tiku_basic.c` exists —
otherwise `make` prints one line saying what was skipped and why.  A clone
of `applications/` on its own then builds everything but `tiku-basic`, and
the root README states the one fact that pins the clone location.

### 2.7 A display for the device

Not this plan's work, but this plan should leave the door where it is.
`tiku_host.h` is nine functions and `tiku_x11.c` is the only file that
knows X11.  The kit gains `kits/interface/host/` with `tiku_host_x11.c`
moved in, and `mk/toolchain.mk` gains `HOST ?= x11` selecting the backend
file.  A framebuffer or device-console host is then one new file in that
directory and one line in the Makefile.  `DISPLAY=` headless runs already
work with no host at all and stay that way.

---

## 3. Phases

Each phase is one capability and lands as one commit (or one commit per
extraction in Phase 3), with `make test` green before and after, and the
counts compared rather than the colour.

| Phase | What | Budget | Done when |
|---|---|---|---|
| **0 — The map** | root `README.md`; one `.gitignore`; delete `_attic-…`; fix the stale paths in HANDOVER; decide `applications.mk` | half a day | a reader can find the desktop shell's source and the apps from the root README alone |
| **1 — One build** | root `Makefile` + `mk/`; objects once; tests from the directory; the apps rule; `make`/`make test`/`make run` at the root; `TIKUOS` for the BASIC host | one to two days | same 74 binaries and same pass counts; `check_shipping` green; touch-one-file rebuild under 3 s |
| **2 — The rename** | `git mv TikuDesktop apps`; `find_apps_dir()` looks for `build/apps`; paths in `run.sh`, the two PLANs, `tiku_font.h` | half a day | `grep -rn TikuDesktop/` finds only binaries and prose |
| **3 — The shell in files** | split `tiku_trk_desktop.c` along its banners, `tracker_run` last | two to three days | no function over 500 lines in `src/shell/desktop/`; `print-ownership` unchanged |
| **4 — The harness** | headers replace the case block; early stop is a failure; (optionally) `run.py` | one day | `run.sh` under 250 lines or replaced; the shutdown-screen test fails when the transition is removed |
| **5 — Generated files** | generators beside their outputs; strings baked at build time; `check-font` | half a day | `git ls-files` shows no generated strings header; `make check-font` exists and passes (*landed:* the font bake is not reproducible across machines — it depends on the faces installed — so `check-font` checks the generator's hash stamped in the bake, not a regeneration) |
| **6 — The device host** | `kits/interface/host/`, `HOST=` selection | when the device work starts | a second host file builds with no change outside its directory |

Phases 0, 1 and 2 are the ones that change what a person types; they are
worth doing together in one sitting.  3 is the largest and the most
mechanical.  4 and 5 are independent of everything else and each other.

---

## 4. Decisions that are yours

1. **Rename `TikuDesktop/` → `apps/`, or keep the name and document the
   inversion?**  Recommended: rename.  The cost is one `git mv` and about a
   dozen path edits; the confusion it removes is the one you hit today at
   the shell prompt.
2. **`run.sh` stays shell, or becomes `run.py`?**  Recommended: headers
   first (Phase 4's first half), which shrinks the shell script enough that
   the question may answer itself.  Rewriting 858 lines of tested harness
   is not free, and the tree's Python tools are checkers, not drivers.
3. **Who owns `strings/`?**  Recommended: the shells keep the catalogue, the
   kit gets the generator and the baked copy at build time.  If a second
   application ever needs its own catalogue, that is the moment to move
   the mechanism up.
4. **`applications.mk`.**  It is the OS Makefile's `-include` hook and
   builds nothing.  Either track it here, empty, as the place a device
   build of an application will be declared — or delete it and the
   `-include` in the OS Makefile with it until there is something to
   declare.
5. **`assets/reference/`.**  A 2 MB reference screenshot is a design input,
   not a product asset.  Keep it out of the repository, or under a
   directory the README names as reference material.

---

## 5. Out of scope

- Splitting `tiku_trk_poseview.c`.
- Changing the script grammar, the conductor channel, or any test's
  assertions: this plan moves tests, it does not rewrite them.
- The kits' internal layout: they are right.
- Anything that changes what `build/TikuDesktop` does on screen.
- The three failures the suite carries today (`findscope`, `unmount`,
  `test_info`'s AW-066, `test_crossmount`'s FS-XM) — all proven
  pre-existing on a pristine tree, and the `typemenus` order-dependence
  in the shared `/tmp/trk_script` home.  Phase 4's per-script fixture
  headers are the likely fix for the last of those, but that is a finding
  to confirm, not a claim.
