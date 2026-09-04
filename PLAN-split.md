# One functionality, one file; one function, one screen

*The combined plan for the shells and the Tracker core (`TikuTracker/src`):
the file split begun in this document's first version, and what the Linux
kernel's coding style adds to it.  2026-09-04, measured on the tree after
milestones 0-2.*

**Status.**  Milestones 0, 1 and 2 landed on 2026-09-04: `tools/split_file.py`
(report, bootstrap, move, cut, sync), `src/ui/poseview/` (13 files) and
`src/shell/tracker/` (12 files).  Two things the work settled beyond the
first plan: a promoted static is renamed with its family's tag (`pv_`,
`browser_`), because the two families both had a `pose_index` and met in
the link; and split files live in a subdirectory with the `tiku_trk_`
prefix kept.  Milestone 3 (the deskbar, eleven files under `src/deskbar/bar/`)
and the first three of milestone 4 (the desktop turn, the script pump and
the script parser, the engine now a family under `src/shell/script/`)
followed the same day.  By the evening every one of the seventeen was cut:
nothing in `TikuTracker/src` is over 200 lines, the largest function being
`browser_script_step` at 157.

## 1. Where the tree stands against the kernel style

The kernel document (`docs.kernel.org/process/coding-style.html`) is
mostly formatting, and on formatting this tree is already consistent.
Measured on the 296 source files of `kits/`, `TikuTracker/src` and
`apps/`, generated data excluded:

| Kernel rule | This tree | Verdict |
|---|---|---|
| 80 columns | 134 lines over, 15 over 100 | keep, and fix the 134 |
| `/* */` only, no `//` | none | already so |
| no trailing whitespace, no tab/space mix | none | already so |
| `} else {` on one line, space after keywords | 423 so; 72 exceptions are one-line aligned `if/else if` tables | keep the idiom |
| lowercase names, no camelCase, no Hungarian | 22 camelCase identifiers, all R5 names in comments | already so |
| braces: omit on single statements | this tree always braces (6 exceptions) | keep ours: stricter |
| centralised exit with `goto` | 51 uses, where it matters | keep |
| macros: uppercase, `do { } while (0)`, no control flow | 0 violations | already so |
| no `#ifdef` in `.c` bodies | 0 in `TikuTracker/src` | already so |

Four kernel rules this tree deliberately does not follow, and will not:
8-column tabs; the return type on the same line as the name; no
`typedef`s for structs; `!ptr` and `sizeof(x)` spellings.  Each is
consistent across the tree (262 typedefs, 4215 `== NULL` to 124 `!p`,
2073 `sizeof x` to 16) and is the TikuOS convention; changing any would
be churn without gain.

What the kernel is strict about and this tree is not is size and
documentation, and those are the two things worth taking:

| Kernel rule | `TikuTracker/src` today |
|---|---|
| a function fits in one or two screens (48 lines) | 1433 functions; 162 over 48, 63 over 100, **17 over 200** |
| five to ten locals, else the function does too much | 31 over five, 2 over ten |
| comment at the head of a function, not inside it | 1007 definitions carry no comment; 273 exported functions are documented in no header either; 180 statics undocumented |
| data declarations must be commented | 1083 struct fields, **898 without a comment** |
| in-body comments only where the code is tricky | 3.1 comment blocks per 100 lines overall; the biggest functions carry 20 to 35 each, each block introducing what should be a function |
| file heads short; long prose belongs in Documentation | 3 file headers over 20 lines, 12 doc comments over 6 prose lines |

The two tables are one finding.  The seventeen functions over 200 lines
are where the in-body comments cluster, because a comment that introduces
a block is a function name that was never written.  Splitting them by
their comments is both halves of the kernel's advice at once.

## 2. The seventeen

Every function over 200 lines in the shells and the core, with the cut
each wants.  The tool's `cut` does the lifting; the names come from the
in-body comments already there.

| Lines | Function | Cut |
|---|---|---|
| 709 | `tiku_trk_script_pump` (script engine) | one function per verb, called from a dispatch table |
| 563 | `tiku_trk_deskbar_draw` | one function per region: clock, tray, application list, the folder |
| 516 | `browser_handle_key` | by key group: navigation, selection, editing, the rest |
| 476 | `desktop_turn_end` | by owner: notice pump and sweep to `_mounts.c`, rename pulse and press-and-hold to `_commands.c`, thumbnails and focus paint to `_draw.c` |
| 460 | `menu_invoke` (desktop) | by command family: file, window, view, find, settings |
| 417 | `tiku_trk_deskbar_click` | by region, mirroring the draw |
| 387 | `browser_pointer_down` | by target: title, navigator, list, count bar, popup |
| 379 | `tiku_trk_poseview_notify` | by notice kind: added, removed, renamed, changed |
| 341 | `tiku_trk_script_open` | the parser: headers, then verbs |
| 332 | `browser_menu_invoke` | by command family, as the desktop's |
| 321 | `tiku_trk_folder_window_draw` | by region |
| 300 | `draw_all` (desktop) | by layer: backdrop, windows, overlays, the bar |
| 271 | `browser_draw` | by region |
| 268 | `tracker_run` (browser) | the pump loop out as `browser_turn_begin`, as the desktop's |
| 259 | `deskbar_pick_dispatch` | by pick kind |
| 247 | `open_popup` (find) | one function per popup |
| 204 | `tiku_trk_poseview_poll` | by stage: the ring, the re-list, the sweep |

A cut is a move of lines into a named function with the state passed in;
the tool does the substitutions and the tests prove the behaviour.  The
ceiling after this milestone is 200 lines; the next pass, when it comes,
takes it to 100, which is where 63 functions sit today.

## 3. Milestones

| # | Milestone | Scope | Cost | Done when |
|---|---|---|---|---|
| **3** | The deskbar | one reading pass to banner the 1485-line head (leaf menu, recents, places, the application list); then `src/deskbar/deskbar/`: `menus`, `layout`, `draw`, `press`, `drops` and a private header, tag `db` | a day and a half | head bannered; no file over 1300 lines; `test_deskbar_dock` and `test_deskbar_prefs` pass |
| **4** | The seventeen | the table in section 2, top down, each with `split_file.py cut` and the in-body comments becoming the names; the desktop turn and the deskbar draw first | three days | no function in `TikuTracker/src` over 200 lines; the suite passes after every cut |
| **5** | Comments the kernel way | (a) every function documented exactly once: exported ones at their prototype in the header, statics at their definition -- the 273 and the 180 written, as contracts (what it does, what it returns, what it refuses); (b) every field of a shared struct commented, the three private headers first (`desktop_t`, `app_t`, the poseview struct) then the public ones, 898 fields; (c) the 12 long doc comments and 3 long file headers shortened, their essays to `TikuTracker/doc/` | three days | `tools/check_style.py` reports zero undocumented functions and zero uncommented fields in private headers |
| **6** | The rule, enforced | `tools/check_style.py`, run by `make lint` beside the comment checker: function ≤ 200 lines, locals ≤ 10, one banner per file, file header ≤ 20 lines, no `//`, 80 columns, every function documented once, every private-header field commented.  An exemption list in the file, each entry with its reason.  Each rule proved by mutation once: a violation added, the check fails, the violation reverted | one day | `make lint` is green with the rules on, and each rule has failed once on purpose |

Order: 3, then 4 and 5 interleaved (a cut writes the comment its new
function needs), then 6 so it lands green.  Eight working days.

## 4. What is left alone, and why

- The kit-side sectioned files (`tiku_fs`, `tiku_query`, `tiku_dl`,
  `tiku_msg`): stages of one job each, read start to end.  The 200-line
  rule applied to `kits/` later would flag nine functions; that is a
  separate pass with its own plan.
- `apps/ide/tiku_ide.c` (3706 lines, `ide_event` at 484): an
  application, with its own plan; it gets the same tool when that plan
  is worked.
- The formatting differences in section 1: decided, not deferred.
- The folder window's six files: one window over six named files; a
  README line in `src/ui/windows/` mapping them is enough.

## 5. Decisions that are yours

1. The ceiling: 200 lines per function now, 100 later, is the proposal.
   The kernel's own number is 48; that would be a rewrite, not a plan.
2. Where the essays go when a file header is shortened:
   `TikuTracker/doc/` beside `spec/`, or the existing `HANDOVER.md`.
3. Whether milestone 5(a) documents the statics too, or only the
   exported functions.  The kernel documents both; 180 statics is a
   day.
