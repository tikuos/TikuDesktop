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
| `differs` | deliberately not the same as Haiku, with the reason |
| `open` | not started, or blocked; the blocker line says on what |
| `WAIVED` | **only the user may waive a row** — never do this yourself |

Under each row you will find one of:

- `**MISSING:**` — what is absent, precisely, in behaviour terms
- `**BLOCKED:**` / `**SUBSYSTEM:**` — what it waits on
- `**PORT:**` — how this port does it, when that differs in mechanism
- `**DIVERGES:**` — where the port deliberately does something else
- `**TIKU:**` — notes about the device-namespace half of the product

**Blocker lines go stale.** Several times a session, a row said "no X exists
anywhere" when X had been built two commits earlier. Verify a blocker against
the code before believing it, and rewrite it when it is wrong — a stale
blocker costs the next person an hour.

### Current state (2026-08-13)

```
889 rows: 476 done · 173 partial · 81 differs · 159 open
core (199): 127 done · 43 partial · 25 differs · 4 open
verification: 1862 checks — 36 unit suites + 49 headless shell scripts
```

---

## 2. Build and test

```sh
cd applications/tracker-new
make                 # builds both shells and every test binary
make test            # unit suites, the contract check, then the shell scripts
python3 tools/check_contract.py    # 889 rows, no duplicates
bash tests/shell/run.sh            # the shell scripts alone
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
key <sym> [ctrl] [shift] [alt]
click|press|move|release <x> <y> [button] [mods]
click-row <n>                 -- see the note below; NOT currently present
wait <turns>
shot <path>
menu-pick <label>
note <text>                   -- the sentence printed with the next verdict
expect-status|-title|-alert|-menu|-count <substring>
expect-rows|-selected|-windows|-columns|-scroll <n>
expect-selected-name|-find-text|-find-type <text>
expect-icon-at <name> <x> <y>        expect-pixel <x> <y> <rrggbb>
expect-file|-no-file|-link|-file-has <path> [text]
expect-desk-icon|-no-desk-icon|-desk-selected|-desk-selected-name
quit
```

`run.sh` recreates the fixture per script, and wipes **every** device state
file and `~/.config/TrackerSettings` between scripts. Per-script extras live
in the `case` block near the top.

### Harness gotchas, all learned the hard way

- **`#` starts a comment anywhere**, so `expect-title find #1` silently
  becomes `expect-title find`.
- **`menu-pick` matches a substring**: "Move to" also matches "Move to
  Trash", and a test once trashed the file it was checking.
- **The Find panel's popup is `f->popup`, not `app.popup`** — `menu-pick`
  cannot drive it. Use coordinates.
- **Coordinates are geometry.** Rows sit at `content.y + 2 + n*row_h`; the
  Deskbar occupies `x >= 866`; the **progress window covers the bottom-right
  corner** (`x 596..976, y 368..`) whenever a job runs. Pick check points
  away from both.
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
src/ui/commands/      folder menu models and application launch presentation
src/ui/windows/       feature windows: folders, Find, Info, file panels,
                      preferences, Select, switcher
src/ui/overlays/      modal alerts and shared operation-status presentation
src/media/            HVIF icons, image decoding, thumbnails, icon painting
src/deskbar/          Deskbar, Desktop view, and application-roster behavior
src/shell/            application descriptors and front-end composition
../desktop/src/       reusable runtime, windows, focus, graphics, native host
```

Headers retain their `tiku_trk_*` names. The Makefile publishes the functional
directories as include roots, so includes describe APIs without encoding where
their current implementation lives.

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

### The 8 core rows still open

| row | what it needs |
|---|---|
| CW-046 | a navigator bar (back/forward/up + location field) |
| MA-006 | display names: "Disks", volume names, the Trash, localised names |
| Q-029 | re-running a template must write back over the same record |
| AW-006 | session restore with workspace, minimised state, decor |
| AW-017 | the Desktop as a *window* — waiver candidate, see the row |
| AW-049 | serving panels to other applications |
| DB-057 | bringing a whole application forward |
| DB-061 | the Deskbar preferences window |

### Clusters, by what the blockers name

| rows | core | cluster |
|---|---|---|
| 33 | 5 | **Icons & art** — halos (need background images), pre-warming (waiver candidate), the offscreen drag bitmap |
| 25 | 7 | **Columns** — largely partial; the remainder wants the menu bar and per-window layout inheritance |
| 24 | 5 | **Queries** — re-run-replaces-the-record, live result windows, per-type result layouts |
| 22 | 3 | **Volumes & mount** — the Mount submenu of partitions, mount events, callers for the unmount sweep |
| 15 | 3 | Drag & drop — spring-loading remainder, the edge fade |
| 12 | 2 | Trash |
| 10 | 2 | **Add-ons** — nothing exists |
| 8 | 1 | Incremental repaint — needs a region/invalidate layer |
| 8 | 1 | **Preferences windows** — unblocks DB-061/062/065, TS-046 |
| 7 | 3 | Replicants / shelf — nothing exists |
| 143 | 20 | unclassified: one-off rows with no shared blocker |

### The single biggest pocket: the menu bar

`tiku_trk_cwmenu.[ch]` builds File / Window / Attributes with every enable
rule, every mark, the per-type submenus and Open-with; `tiku_trk_menubar_*`
draws and hit-tests it; `tests/test_menu.c` covers the model. **Neither shell
draws it.** That single gap holds CW-022, CW-025, CW-042, TS-023 and the
Attributes work off the screen.

I built it, saw it work (File / Window / Attributes drawn in the window, a
title opening its menu, "New folder" from that menu creating the folder), and
**reverted it**, because the bar takes 20 px at the top of every folder
window and 14–18 shell scripts encode the old row positions. Do it in this
order:

1. **Land a `click-row <n>` harness op first, alone.** It resolves a row's
   pixel through the shell's own geometry (`row_point` callback on the script
   state, exactly like `pose_at`), so chrome changes stop invalidating tests.
   Convert one script by hand, prove it, commit.
2. **Convert genuine list-row clicks, script by script, reading each one.**
   `band`, `drop`, `dropcol`, `info`, `panel`, `session1/2`, `link`. Do *not*
   do this with a regex: icon-mode clicks are positions, not rows, and a
   blanket conversion silently rewrote them and broke the icon tests.
   Clicks on window tabs, the Deskbar, the Info window, the Find panel and
   the preferences window must not move.
3. **Then** add the bar: draw it into the top strip in `folder_draw`,
   subtract its height from `list`, add the same offset in `folder_row_at`
   and in the row-point callback, and route a press in the strip through
   `tiku_trk_menubar_title_at` → `tiku_trk_menubar_panel` → the existing
   `menu_invoke`. Track `bar_fl` / `bar_open` in the shell, because one menu
   model is drawn per window.

### Cheapest closes on the board

Partials whose `MISSING:` is a single clause: PVS-024 (band starts on the
press, not after the threshold), PVN-048, CW-050, PVK-052, PVL-035, PVL-083,
CW-022, CW-042, CW-043, PVN-017, PVN-044, PVS-050 (horizontal auto-scroll).
Roughly thirty of them. They are spread across areas, so sweeping them
re-tests broadly.

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

---

## 9. Where to start on day one

1. `make test` — see 1672 green, and read the last ten commits.
2. Pick a cluster from §7. Run the audit yourself first:
   read `FEATURES.md` for every `open`/`partial` row in the area and check
   each blocker line against the code, because some are stale.
3. Take the cheapest complete behaviour in it, and follow §3 exactly.
4. Present the commit message. Wait.
