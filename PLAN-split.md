# One functionality, one file

*A plan for the four shell-side monoliths, 2026-09-04.  Follows
PLAN-structure.md, whose phases 0-6 landed; the measurements below are of
the tree at 8b53df7.*

## 1. What the rule finds

The rule is the one the desktop shell already keeps after its split: a
file owns one functionality, is named for it, and a reader can find it
from the name alone.  Measured against that, nine files carry several
functionalities under section banners and hold a third of the tree's
93,000 lines.  Four of them are shell-side and are where the next bug
lands; the kit-side ones (`tiku_fs`, `tiku_query`, `tiku_dl`, `tiku_msg`)
are stages of one job each and stay.

| File | Lines | Functions | What is inside | Shared across sections |
|---|---|---|---|---|
| `src/ui/tiku_trk_poseview.c` | 6529 | 221 | 14 sections: list access, loading, batches, selection, keyboard, type-ahead, sorting, icon layout, rename, spatial memory, notices, viewport and extent, liveness and drawing, saved columns | 4 file-scope statics; each section calls 1-9 statics of the others |
| `src/shell/tiku_trk_app.c` | 4067 | 63 | the TikuTracker browser shell in one file: `tracker_run` 1125 lines (arguments, boot, a 912-line loop, teardown), `handle_key` 516, `menu_invoke` 332, `draw` 271, navigation, rename, script hooks | none: it is one function talking to itself |
| `src/deskbar/tiku_trk_deskbar.c` | 3564 | 116 | an unbannered 1485-line head (the menus), then layout, drawing, interaction, drops | the drawing and interaction sections call 10-11 statics of the head |
| `src/shell/desktop/tiku_trk_desktop_events.c` | 2985 | 6 | the four event cases, and `desktop_turn_end`: 476 lines doing spring tracking, thumbnails, press-and-hold, the rename pulse, focus paint, the device pump, the sweep, the 400 ms notice pump (170 lines), the shot, the frame, progress, deadline, closing, the script clock | none |

The banners are the split, already written down: every section title
is a file name.  The head of the deskbar and the loop of the browser
shell are the two places with no banner, and they need one pass of
reading before they can be cut.

What guards the cuts: `test_poseview.c` (1977 lines), `test_deskbar_dock.c`
(1032), `test_deskbar_prefs.c`, 39 shell scripts that run the browser
shell, and `make print-ownership`, which must not change.

## 2. The method, once

The desktop split (PLAN-structure phase 3) settled the method, and it is
reused unchanged:

1. **A private header per split**: `tiku_trk_<x>_private.h` holds the
   includes, the macros, the struct, the inline accessors, and a
   generated block of prototypes for every function one file defines
   and another uses.  Nothing becomes public that was static.
2. **Moves in source order**, one section per file, the file named for
   the banner.  A function is never edited in the move; forward
   declarations are generated, not written.
3. **The tool does the move**: bootstrap, move, sync, report.  Its
   bugs are known (single and multi-line forward declarations, static
   declarations copied into the header, `&app` losing its `&`,
   `= ev;` copies, appending out of source order) and each has a check.
4. **Proof is the diff of behaviour, not of text**: the same tests pass
   before and after, `print-ownership` is byte-identical, and the
   stripped object code of the split files links to the same symbol
   set.

## 3. Milestones

| # | Milestone | Scope | Cost | Done when |
|---|---|---|---|---|
| **0** | The tool in the tree | `tools/split_file.py` from the desktop split's scratch tool, with its checks; a `--report` that lists a file's sections, their sizes and cross-section calls | half a day | `python3 tools/split_file.py --report src/ui/tiku_trk_poseview.c` prints the table in section 1 |
| **1** | Poseview | `src/ui/poseview/`: `_private.h` + `list.c`, `load.c` (loading and batches), `select.c`, `keys.c`, `typeahead.c`, `sort.c`, `iconmode.c`, `rename.c`, `spatial.c`, `extent.c` (notices, viewport and extent), `draw.c`, `columns.c`; the public `tiku_trk_poseview.h` unchanged | two days | no file over 1300 lines, none with two banners; `test_poseview` and the scripts pass; ownership unchanged |
| **2** | The browser shell | `src/shell/tracker/`: `_private.h` + `lifecycle.c` (arguments, boot stages, teardown), `events.c` (the loop's cases: key, pointer, tick), `commands.c` (`menu_invoke`), `draw.c`, `navigate.c`, `rename.c`, `script.c` (the script hooks); `tracker_run` becomes the loop between the calls, as the desktop's did | two days | `tracker_run` under 150 lines; no function over 300; the 39 scripts pass in the tracker lane |
| **3** | The deskbar | one reading pass to banner the 1485-line head (the leaf menu, recents, places, the application list); then `src/deskbar/deskbar/`: `menus.c`, `layout.c`, `draw.c`, `press.c`, `drops.c` + `_private.h` | one and a half days | head bannered; no file over 1300 lines; `test_deskbar_dock` and `test_deskbar_prefs` pass |
| **4** | The turn | `desktop_turn_end` split by owner, not by a new file: the notice pump to `_mounts.c`, the sweep with it, the rename pulse and press-and-hold to `_commands.c`, thumbnails and focus paint to `_draw.c`, shot, frame, deadline and closing stay as the turn; each a named function called in the same order | one day | `desktop_turn_end` under 80 lines and reads as a list of what a turn does |
| **5** | The rule, enforced | `kits/tools/check_architecture.py` gains two limits under `make check-architecture`: no function over 300 lines and no file with more than one section banner; the four files above are the only ones it would have caught, so it goes in green | half a day | the check fails when a banner is added back into any split file (proved once, then reverted) |

Milestones 1 and 2 are independent of each other and each other's
biggest; 3 and 4 are independent of everything.  5 last, so that it
passes the day it lands.  The whole plan is a working week.

## 4. What is left alone, and why

- `tiku_fs.c`, `tiku_query.c`, `tiku_dl.c`, `tiku_msg.c`: nine, four,
  five and seven sections, but each is one job read start to end (a
  queue and its work; matching, rendering, running; bytes to wire to
  reader).  BeOS kept the equivalents as one class each.  The size
  limit in milestone 5 would flag `tiku_fs.c`'s Trash section at 1024
  lines; it is exempt by name in the check, with the reason written
  there.
- `apps/ide/tiku_ide.c` (3706 lines, five sections): an application,
  not the shell, and the IDE has its own plan (`apps/PLAN-ide.md`).  It
  gets the same treatment when that plan is worked, with milestone 0's
  tool.
- The folder window's six files: the opposite case, one window spread
  over window, edit, lifecycle, actions, file actions, menu.  Each has
  a name and a reason; a README line in `src/ui/windows/` mapping the
  six is enough.

## 5. Decisions that are yours

1. Whether milestone 2 lands before or after 1.  The browser shell is
   the one that ships on a device, which argues for first; poseview is
   where the tests are thickest, which argues for practising there.
2. Whether the split files live in a subdirectory (`src/ui/poseview/`)
   or beside the old file with a suffix (`tiku_trk_poseview_select.c`).
   The desktop split used the suffix form and it is already 14 files
   wide; the subdirectory form is proposed here.
3. The limits in milestone 5: 300 lines per function and one banner
   per file are what the tree after milestones 1-4 satisfies with
   room.  Tighter is possible later; looser defeats the point.
