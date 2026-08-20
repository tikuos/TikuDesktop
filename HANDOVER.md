# Handover — the new Tracker for TikuOS

You are picking up a port of Haiku's Tracker and Deskbar. This document is
what you need to start from nothing and keep going. Read it before you read
code, and read `FEATURES.md` before you write any.

---

## 1. What this is

A rewrite of Tracker and Deskbar for TikuOS, in C, against a drawing floor
that lives in `applications/desktop/` (surface, font, R5 controls). Two
front-ends are built from one core:

- **`build/desktop`** — the multi-window shell: a backdrop with a Desktop
  pose view, folder windows, the Deskbar, the Find panel, Get Info, the
  Open/Save panel, alerts, a progress window.
- **`build/tracker`** — the single-window shell, with the column strip, the
  scrollbar and the status view.

Each is built in **two flavours from one source tree**. `build/desktop` and
`build/tracker` are the product: they link a stub that refuses `-script`, and
carry neither the script interpreter nor the presentation adapter that reads
rows, menus and pixels back out of a running shell. `build/dev/desktop` and
`build/dev/tracker` link that engine and are what `tests/shell/run.sh` drives.
The test infrastructure is a developer tool, not something a device image
should carry; `tools/check_shipping.py` (run inside `make test`) proves the
script vocabulary is absent from what ships and present in what the harness
runs. If you are running a shell by hand and it says *"this build has no
script engine"*, you wanted `build/dev/`.

**A behaviour proven in one front-end is not proven in the other.** Rows say
which shell they were tested in when it matters (`*Shell: desktop*`).

### The contract

`FEATURES.md` is the fidelity contract: **889 rows**, each one behaviour read
from Haiku's own source, with its source citation. It is the spec, the
progress ledger and the audit trail. Every row has a verdict:

| verdict | meaning |
|---|---|
| `done` | the behaviour holds here, and something proves it |
| `partial` | some clauses hold; the `MISSING:` line says exactly which do not |
| `differs` | another mechanism reaches the row's outcome; the `PORT:` line names it |
| `open` | not started, or blocked; the blocker line says on what |
| `WAIVED` | **only the user may waive a row** — never do this yourself |

Under each row you will find one of:

- `**MISSING:**` — what is absent, precisely, in behaviour terms
- `**BLOCKED:**` / `**SUBSYSTEM:**` — what it waits on
- `**WIRING:**` — the code exists and nothing calls it: the cheapest rows here
- `**WAIVER CANDIDATE:**` — the premise cannot exist in this port; only the
  user may strike it
- `**PORT:**` — how this port does it, when that differs in mechanism
- `**DIVERGES:**` — where the port deliberately does something else
- `**TIKU:**` — notes about the device-namespace half of the product

**Blocker lines go stale.** Several times a session, a row said "no X exists
anywhere" when X had been built two commits earlier. Verify a blocker against
the code before believing it, and rewrite it when it is wrong — a stale
blocker costs the next person an hour.

### Current state (2026-08-15)

```
889 rows: 556 done · 235 partial ·  91 differs ·  7 open
core (199): 129 done ·  47 partial ·  23 differs ·  0 open
verification: 2019 checks — 44 unit suites + 58 headless shell scripts
```

Nothing `core` is open: what remains is 235 rows where a named clause does
not hold and 91 where this port does it another way.  The open seven are all
premises this port cannot hold — no date-format setting, no inter-application
messaging, no locale catalogue, no version resource, no message-archive drop,
no force-terminate primitive — and six of them are waiver candidates only the
user may strike.

**`differs` is not a synonym for handled.** It requires an alternative
implementation that reaches the row's outcome, written out on a `PORT:` line.
A note that concedes a clause of the rule ("the scroll bar is not highlighted
with it") is a `partial` with a `MISSING:` line, and a row whose premise
cannot exist here is a waiver candidate.  An audit on 2026-08-14 moved
eighteen rows back for exactly that.

---

## 2. Build and test

```sh
cd applications/tracker-new
make                 # builds both shells and every test binary
make test            # unit suites, the contract check, then the shell scripts
python3 tools/check_contract.py    # 889 rows, no duplicates
bash tests/shell/run.sh            # the shell scripts alone
TRK_SHELL_ONLY=drop.script bash tests/shell/run.sh   # one of them
```

`make test` must end with **`shell scripts hold in both front-ends`** and no
`FAILURES`. Count the passes (`make test | grep -c '\[PASS\]'`) — a suite that
silently stopped running still prints no failures.

### Traps in the build system

- **Every test target lists its sources by hand** in the `Makefile`. Adding a
  dependency to a shared file (e.g. making `tiku_trk_nodeicon.c` call into
  `tiku_trk_types.c`) breaks six targets at link time. Grep the Makefile for
  the file you touched and add the new source everywhere it appears.
- **A test binary in `TESTBINS` is not necessarily run.** `test_launch` sat
  built-but-never-run for a whole session because it was missing from the
  `test:` recipe. If you add a suite, add it to both.
- **The harness runs `build/dev/`, not `build/`.** A script handed to a
  shipping shell is refused loudly rather than ignored, so a lane pointed at
  the wrong binary fails instead of passing quietly. Both flavours are built
  by a bare `make`.
- **Do not read a pass from a stale binary.** If `make` fails, `run.sh` will
  happily run the previous build and report green. I was fooled by this
  twice. Never pass `-s` to make while debugging, and check that the error
  output is empty before you believe a test result.

---

## 3. How to work on a row

This is the method that has held, and every deviation from it produced a
false pass.

1. **Read the row.** The headline, the Haiku source paragraph, and the
   blocker line. Then verify the blocker against the code.
2. **Write the check first**, in the suite that fits (see §4).
3. **Make it fail.** A new check that passes immediately is proving nothing
   until you have seen it fail on the input it guards.
4. **Implement.**
5. **Mutate.** Break the thing you just built — one line, the smallest
   change that removes the behaviour — rebuild, and confirm the check
   fails. Restore. If a mutation *survives*, either your check is vacuous
   (fix it) or the mutation is equivalent (say so, in writing).
6. **Update the row.** `MISSING:` shrinks to what is *still* absent, or
   becomes `PORT:` and the verdict becomes `done`.
7. **Full suite**, then the commit message (see §6).

### Vacuous checks — the recurring failure

Every one of these got through a first draft this session:

- Comparing a value against **itself** (the expected and the actual came
  from the same call).
- A needle silently truncated to `""` by a too-small buffer, so `strstr`
  matched anything. (`text2` in the script harness was 16 bytes.)
- Asserting "not X" where **empty** also satisfies it — assert the positive.
- A test block placed **after** the teardown that frees what it tests: it
  runs against freed memory or a NULL view and reports nothing.
- Asserting a state that was already true before the action.

If a mutation survives, assume the check is at fault before you assume the
mutation is equivalent.

---

## 4. The test surfaces

### Unit suites — `tests/test_*.c`

Plain C, one `check(cond, "ROW-ID", "what this proves")` per assertion. The
text is read by humans: say what the behaviour *is*, not what the code does.
Run one directly: `./build/test_poseview`.

### Shell scripts — `tests/shell/*.script`

Drive the **real binaries** headless (`DISPLAY=` empty) through
`-script <file>`. One `XOpenDisplay`-free surface; the shell draws into its
own surface either way, so a scripted run exercises the real draw path.

Grammar (see `src/shell/tiku_trk_script.h` for the full list):

```
# shell: desktop        which binary runs it (desktop | tracker)
# home: fixture         run with HOME pointing at a fixture (Desktop tests)
# apps: fixture         install an application registry that records launches
# argv: none            start with no path argument
# work: home            put the fixture under $HOME (anything about the Trash)
key <sym> [ctrl] [shift] [alt]
click|press|move|release <x> <y> [button] [mods]
click-row <n> [button] [mods]           -- a LIST row, by index
press-row|move-row|release-row <n> ...  -- one half of a gesture, by row
click-window|drag-window <title> [x y]  -- raise or move a window by title
wait <turns>
shot <path>
menu-pick <label>             -- popups, menu bars, and the Deskbar leaf menu
note <text>                   -- the sentence printed with the next verdict
expect-status|-title|-alert|-menu|-count <substring>
expect-rows|-selected|-windows|-columns|-scroll <n>
expect-selected-name|-find-text|-find-type <text>
expect-icon-at <name> <x> <y>        -- where an icon sits IN the view
expect-icon-screen <name> <x> <y>    -- where it sits on the SCREEN
expect-window-at <x> <y>             expect-pixel <x> <y> <rrggbb>
expect-file|-no-file|-link|-file-has <path> [text]
expect-desk-icon|-no-desk-icon|-desk-selected|-desk-selected-name
quit
```

**Aim at things by name, not by pixel.** Every row, tab and menu row can be
addressed through the shell's own geometry, so chrome can move without
rewriting tests — which is what the menu bar's 20 px cost the first time it
landed.  A coordinate belongs in a script only when the coordinate IS the
behaviour: a drop point, a band, a point over the backdrop.

`run.sh` recreates the fixture per script, and wipes **every** device state
file and `~/.config/TrackerSettings` between scripts. Per-script extras live
in the `case` block near the top.

Every script also runs with **`HOME` pointing at `/tmp/trk_home`**, not at the
account. Run against the real home, a drag that misses its window drops files
into the user's Desktop, and the next Desktop test then reads a folder covered
in the last one's leftovers. `session1`/`session2` are the deliberate
exception: part two starts from what part one left in that home, so the home
is not wiped between them.

### Harness gotchas, all learned the hard way

- **`#` starts a comment anywhere**, so `expect-title find #1` silently
  becomes `expect-title find`.
- **`menu-pick` matches a substring**: "Move to" also matches "Move to
  Trash", and a test once trashed the file it was checking.
- **The Find panel's popup is `f->popup`, not `app.popup`** — `menu-pick`
  cannot drive it. Use coordinates.
- **Coordinates are geometry.** A folder window's content starts below a tab
  and a menu bar; the Deskbar occupies `x >= 866`; the **progress window
  covers the bottom-right corner** (`x 596..976, y 368..`) whenever a job
  runs. Pick check points away from both, and prefer `click-row` /
  `click-window` to arithmetic.
- **A verdict prints what it FOUND**, not what it expected, for titles,
  window positions, icons and selections. A failure that repeats the
  expectation tells you nothing; if you add a check, make it report the
  actual value the same way.
- **A release over the backdrop is a drop** — it moves the file to the
  Desktop. End a drag where it began if you only mean to test the drag.
- A **double click is time-based**: two `click`s at the same spot in a
  headless run are usually within the window whatever waits you insert. A
  click elsewhere in between does not reset it unless the code says so.
- `expect-selected` reads the **focused window's** view. The Desktop has no
  window and is never focused — use `expect-desk-*` for it.

---

## 5. The code, in one page

```
src/core/             UI-independent models, commands, queries, and state
src/backends/         host/device namespace access, notices, file operations
src/ui/               pose views, menus, navigation, and visual controllers
src/ui/commands/      folder/file commands, drops, menus, launch presentation
src/ui/windows/       feature windows: folders, Find, Info, file panels,
                      preferences, Select, switcher
src/ui/input/         drag targets and rubber-band gesture controllers
src/ui/overlays/      modal alerts and shared operation-status presentation
src/media/            HVIF icons, image decoding, thumbnails, icon painting
src/deskbar/          Deskbar, Desktop view, and application-roster behavior
src/shell/            application descriptors and front-end composition
src/shell/adapters/   script-facing presentation adapters
src/shell/session/    restore, persistence, and bounded shutdown services
../desktop/src/       reusable runtime, windows, focus, graphics, native host
```

Headers retain their `tiku_trk_*` names. The Makefile publishes the functional
directories as include roots, so includes describe APIs without encoding where
their current implementation lives.

Four places you will look for first:

- the **menu model** is `src/ui/tiku_trk_cwmenu.c` (File / Window / Attributes
  and the four context menus, with every enable rule and mark); the **bar**
  is drawn from `src/ui/windows/tiku_trk_folder_window.c` in the desktop shell
  and `src/shell/tiku_trk_app.c` in the single-window one
- the **script harness** is `src/shell/tiku_trk_script.c` (grammar and pump)
  plus `src/shell/adapters/tiku_trk_script_adapter.c` (what the shell tells
  it: row points, tab points, icon positions, menu labels)
- **pose geometry, columns, sorting and filtering** are all one file,
  `src/ui/tiku_trk_poseview.c` — it is the largest thing here by far
- `../desktop/src/` is a separate library with its own `tiku_desk_*` prefix:
  surface, font, windows, focus, X11 host. Cite it by that path

Conventions: `tiku_trk_` prefix, K&R braces, 4 spaces, **80 columns**, no
dynamic allocation in the hot paths, Doxygen on anything public.

### Comments

`hygiene/commentstyle.md` in the tikuOS root governs both comments and
commit messages, and `make lint` does **not** check most of it. The rule that
matters: a comment states the **live hazard or the reason**, never the route
you took to find it. No "used to", no "turned out", no milestone markers.

---

## 6. Committing

Non-negotiable, from `CLAUDE.md`:

- **Present the subject, author and body for review, and wait for the
  go-ahead.** Every commit, including amends, batched when several land
  together.
- Author is the repo's identity, `Ambuj Varshney <ambuj.varshney@proton.me>`.
  Plain `git commit` — never `-c user.email=`.
- **No `Co-Authored-By` trailer.** Never push.
- Message shape: `Area: what now works`, then **at most 5 one-line bullets**.
  A `commit-msg` hook refuses anything else; fix the message, never
  `--no-verify`.
- **Verify what landed**: `git show --stat HEAD`. A commit once captured 4
  files out of 21 because a `git stash`/`pop` cycle had quietly unstaged
  everything after `git add -A`.

---

## 7. What is left

### The open fourteen

Nothing `core` is open. Every open row is one of five absences, and four are
waiver candidates the user alone may strike:

| rows | what they wait on |
|---|---|
| PVL-071, CW-020, IV-049, AW-016 | **background images** — the backdrop is a flat fill; no image loading, no placement, nowhere to hang one |
| PVL-079, TS-028 | **a date-format setting** — nothing in the port has one, so "the format changed" has no source to fire from (waiver candidates) |
| PVL-025, PVL-098 | **a caller** — the drop-point seed is written and reachable, and New folder never uses it, so a new folder is auto-placed |
| AW-034, MA-007, MA-032, PVL-086 | premises this port cannot hold: inter-application messaging, a name catalogue, a version fact, an archived-object drop (waiver candidates) |
| DB-020, AW-076 | a force-terminate primitive; a context menu outside a pose view |

### The single-window shell shares the engine, not the commands

`build/tracker` has **no reference to the file-action controllers**: its own
inline delete, no file clipboard, no alert layer.  So the delete question,
the mixed-clipboard paste, Duplicate, Create link and Identify are
`build/desktop` behaviours, and a file-command row reading `done` means done
there.  `tools/sweep_shells.sh` measures it -- every desktop script run
against the other shell, 49 of 49 failing at least one check as of
2026-08-15.  Wiring `tiku_trk_folder_file_actions_*` into the app shell is
the piece of work that closes the whole class; the harness ops to prove it
(`click-row`, `click-cell`, `press-row`) already work in both.

### Where the 229 partial rows cluster

Counted by what their `MISSING:` lines name, so a subsystem that unblocks
many rows shows up as one:

| rows | blocker |
|---|---|
| 13 | **mount service** — the Mount submenu of partitions, mount/unmount events |
| 12 | **incremental repaint** — everything here redraws whole frames; several rows are about repainting only the strip that changed |
| 8 | **dynamic loading** — the add-on registry, menu and shortcuts are real; loading a shared object and running it on its own thread is not |
| 8 | **inter-application messaging** — refs, messengers, a roster of other teams |
| 6 | printers folder · 5 alerts as modal questions · 5 replicants/shelf |
| ~170 | one-off clauses with no shared blocker — this is where the volume is |

By area the partials sit at AW 41 · CW 28 · DB 26 · MA 25 · PVL 24 · TS 19 ·
IV 18 · PVN 14 · PVS 12 · PVK 12 · FS 5 · Q 5.

### The menu bar is built

It is drawn in both shells, its titles open their menus, the enable rules and
marks are live, and the four rows it used to hold off the screen (CW-022,
CW-025, CW-042, TS-023) are now `partial` on much narrower clauses —
printers-folder specialisation, service-backed mount and add-on verbs, Copy
layout / Paste layout, and the last-column alert. The harness ops it forced
into existence (`click-row`, `press-row`, `click-window`) are the reason the
next piece of chrome will be cheaper.

### Cheapest closes on the board

Partials whose `MISSING:` is a single clause: DB-068, DB-033, TS-060, AW-059,
AW-054, AW-052, AW-031, AW-007, MA-070, MA-021, MA-006, IV-028, IV-041,
IV-004, PVN-027, FS-066 — roughly thirty in all, spread across areas, so
sweeping them re-tests broadly. `PVL-025`/`PVL-098` are cheaper still: the
code exists and nothing calls it, and the same is true of the
`TypeAheadFiltering` preference, which the Windows page writes and nothing
reads.

---

## 8. Things that will bite you

- **`applications/` is gitignored in the tikuOS mainline.** This is its own
  repository; commit inside it. So are `tikukits/` and `TikuBench/`.
- **The store has three kinds** (xattr, sidecar, memory) and reserved node
  keys (`\x01panel`, `\x01session`, `\x01types`, `\x01queries`, `\x01apps`).
  The desktop shell must use the **sidecar** store — it once used the
  volatile one and discarded every saved query, frame and recent list at
  exit while reporting success.
- **Positions are stored unscaled at 32 px** and scaled on the way out. Do
  arithmetic on them in stored space; a screen round-trip is not idempotent
  at 1.5×, which made a "clean up" move the same icon every time it ran.
- **`/tmp` is a different device from `$HOME`**, so it has its own state
  file. `run.sh` wipes them all; anything you write by hand must too.
- **A volume is found by device**, not by path prefix, so a test table of
  invented devices answers NULL for everything and never reaches the rules
  under test.
- **Sort keys, columns, view mode and both scroll origins now persist per
  folder.** A test that assumed a fresh view's defaults must set what it
  depends on explicitly.
- The device backend's types are `tiku-*`; the POSIX backend derives a type
  from the file name. Both prefix forms (`text/`, `tiku-`) are supertype
  claims in the type database and the application registry.
- **Nothing creates its own directories for you.** Settings, the thumbnail
  cache and the Deskbar folder each make their parents through
  `tiku_trk_state_mkparents`; a write that skips it silently does nothing on
  a home that has never run this before.
- **An icon view's scroll extent always contains the window's own corner.**
  Without that the extent starts at the first icon, and dropping icons low in
  a window scrolls the window to meet them — so what was dropped is not where
  it was let go.

---

## 9. Where to start on day one

1. `make test` — see **1948** green and `shell scripts hold in both
   front-ends`, then read the last ten commits.
2. Pick a cluster from §7. Run the audit yourself first: read `FEATURES.md`
   for every `open`/`partial` row in the area and check each blocker line
   against the code, because some are stale — the 2026-08-14 audit found
   notes claiming a subsystem did not exist two commits after it was built,
   and notes claiming a mechanism that had no caller.
3. Take the cheapest complete behaviour in it, and follow §3 exactly.
4. Present the commit message. Wait.
