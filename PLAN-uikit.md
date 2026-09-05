# One toolkit, two ends

*A plan for the whole of the desktop's interface kit reaching a TikuOS
board, so a board program builds any window the desktop can show, and a
control added to the desktop reaches the board by the same act.
2026-09-05.*

## 1. What is there, and what the gap is

The board has a wire and primitives (PLAN-gui.md, milestone 6): a window
session over the console, a writer for eighteen of the display list's
twenty-seven ops, events back, text metrics.  What it lacks is what makes
the desktop's kit a kit rather than a set of drawing calls:

- **Behaviour.**  A field keeps a caret and edits; a slider follows a
  drag; a list keeps a selection and scrolls; tabs switch; a menu opens,
  walks and picks; an alert asks and answers.  On the desktop these are
  C in `kits/interface/`.  On the board a program has none of them and
  hit-tests rectangles by hand.
- **The rest of the session.**  Menus, raise, place, sizeable, and the
  SAY/TELL messages that give an application the file picker and the
  clipboard.
- **A way to keep the two ends the same.**  Today the board's writer is
  a second copy of the desktop's record layouts, typed by hand, and the
  desktop's reader, player and narrator are three more copies of the
  same table.  Nothing catches drift except a test.

The desktop's own applications (`apps/edit`, `apps/term`, `apps/ide`)
already work the way a board program must: state objects, a paint
function that draws the whole window from them each time, events fed to
the state objects.  So the target is not a new programming model.  It
is the same one with the kit's behaviour compiled for the board and the
wire generated from one table.

Three facts about the kit decide the shape:

1. **Some controls are already pure.**  `tiku_field` (a line being
   edited), `tiku_scroll` (a scrollbar's position), `tiku_textview`
   (many lines with a caret) include nothing that draws.  They compile
   on the board as they are.
2. **The rest mix behaviour and paint in one file.**  `tiku_list`,
   `tiku_slider`, `tiku_tabs`, `tiku_menu`, `tiku_alert`, `tiku_colors`,
   `tiku_layout` each carry a `_draw` beside the state they keep.  The
   state parts have no drawing in them; they are in the wrong file.
3. **The display list is already the boundary.**  Every control paints
   through `tiku_ui`, which records an op when the surface records.
   A control's op is its only wire form, and the op set is a table.

So: split the mixed files so their behaviour is a source both ends
compile; put the op table in one place both ends generate from; and
give the board a surface that records rather than paints, so the same
paint code runs on both ends.  After that, a new control is one
behaviour file, one op row and one painter, and the board has it the
moment the desktop does.

## 2. Milestones

| # | Milestone | Scope | Cost | Done when |
|---|---|---|---|---|
| **0 ✓** | The op table generated | `kits/interface/dl_ops.txt`: one row per op with its number, name, fields (i16 x4 rect, u32 colour, u16 per-mille, u8 face, string...) and what it says; `kits/interface/tools/gendl.py` writes `tiku_dl_ops.h` (the numbers, the payload lengths, the field readers), which `tiku_dl.c`'s recorders, `fixed_payload`, the player's decoding and the fact iterator use instead of their hand-written copies, and `board/gui/tiku_draw_ops.h` (the board's writers into a fixed buffer); `make check-dl` regenerates and diffs, under `make test`; `test_gui` already proves the board's bytes against the desktop's reader | two days | the desktop draws every window as before (the whole suite), the board's eighteen writers are generated not typed, and a row added to the table gives both ends the op with no other edit but the painter |
| **1 ✓** | The remaining ops on the board | scrollbar, tabs, alert icon, bevel, clip set and reset, icon by reference, picture, from the table; the board's list buffer sized by the window (`tiku_draw_open` takes a buffer, so a program with one large window and one small pays for what it draws); icons and pictures carry a byte budget per window and refuse past it | one day | `test_gui` narrates every op the desktop has; a board window shows an icon and a scrollbar |
| **2 ✓** | Behaviour split from paint | each mixed control becomes two files: `tiku_<x>.c` keeps the state and the input (press, drag, key, wheel), `tiku_<x>_paint.c` draws it; the header declares the state's API without any drawing include, and the paint header includes it; `kits/tools/check_kits.py` gains a rule: a behaviour file includes no `tiku_gfx.h`, `tiku_font.h` or `tiku_ui.h`, so a behaviour cannot drift back into paint unnoticed; measured text stays behind one seam, `tiku_measure(face, text)`, which the desktop answers from its fonts and the board from `tiku_metrics.h` | three days | the desktop suite passes unchanged; `make check-kits` fails on a behaviour file that includes a paint header (shown to fire); every behaviour file compiles with `arm-none-eabi-gcc` against the board's stub headers |
| **3 ✓** | The kit on the board | `applications.mk` compiles the behaviour files from `kits/interface/` into the firmware behind `TIKU_APPL_GUI` (the overlay reads the desktop's sources in place; nothing is copied); the board's `tiku_draw_window_t` grows a recording surface: `tiku_ui_*` calls made against it record ops instead of painting, so the desktop's paint code paths that only call `tiku_ui` run on the board unchanged; a board program declares `tiku_field_t`, `tiku_slider_t`, `tiku_list_t`, `tiku_tabs_t`, feeds them events with the same calls the desktop's applications use, and paints them with the same `*_paint` calls | three days | `apps/examples/counter.c` and `apps/examples/hello.c` build for the board from their own sources with a board `main` of ten lines, open on the desktop, and take input; `boarddraw.script` drives a field, a slider and a list on the board |
| **4 ✓** | The rest of the session | a compact menu message: a menu set as text lines (`menu File`, `item Open… o cmd`, `sep`) the desktop expands into its own menu set, so a board carries a hundred bytes rather than ten kilobytes; PICK from the desktop's menus back to the board as a command; raise, place and sizeable; up to eight windows; resize events; SAY/TELL carried as flattened messages with a board-side `tiku_msg` reader (the format is byte-at-a-time already), so the board can ask for the file picker and hear the path, and set and read the clipboard | two days | a board window has a menu bar whose pick reaches the process; `boardmenu.script` picks an item and reads the board's answer; a board asks for a file and hears its path |
| **5** | Layout without numbers | `tiku_layout` (a rectangle divided as described) compiled on the board from milestone 2, with the board's metrics behind the measure seam, so a program says rows and columns and weights and never a pixel; a form-like helper that places a column of labelled controls the way the described panel does | one day | the counter example's window lays itself out from a layout description and looks the same on the desktop and from the board |
| **6** | The application runtime for the board | `tiku_app_descriptor_t` (`start`, `stop`, `event`, `tick`, `pick`, `picked`, `closed`) is the desktop's application shape; a board runner `tiku_draw_run(descriptor, process)` drives it from a process's events, offering the same `tiku_app_services_t` table (open, present, menus, close, place, pick, raise, sizeable, clip) over the console; a descriptor builds as a desktop application, an embedded one, or a board one, from one source | two days | `apps/examples/sketch.c` runs on the board unchanged and on the desktop unchanged; `embedapp.script`, `remoteapp.script` and a new `boardapp.script` assert the same behaviours of the same descriptor |
| **7** | Keeping them the same | `make check-toolkit` under `make test`: the op table is generated and current, every behaviour file compiles for the board, every op the desktop can narrate the board can write, every service in the runtime table has a board implementation or a documented refusal; a page in `kits/interface/doc/toolkit.md` that says how to add a control: the behaviour file, the op row, the painter, the narration words, and nothing else | one day | a new control added by that page's steps appears on the board with no change under `board/`; the check fails when any of the four is missing (shown to fire) |

Milestone 0 landed the same day: `kits/interface/dl_ops.txt` names every
op with its fields in wire order; `gendl.py` writes `tiku_dl_ops.h`
(numbers, lengths, where text starts, names), which `tiku_dl.c` reads
and checks by, and `board/gui/tiku_draw_ops.h`, the board's writers, so
the board's own hand-typed writers are gone.  `make check-dl` runs
under `make test` and was shown to fail on a stale header.  One
lesson: the reader's "least length" belongs to text tails only, and
tabs carry NUL-terminated labels, so the table has a `strs` tail kind
beside `str`, `blob16` and `bytes`.

Milestone 1 followed from it: every op of the table has a writer, so
the nine the board lacked (scrollbar, tabs, alert icon, bevel, clip set
and reset, icon art and placement, picture) came with the generator,
and `test_gui` writes one of each and has the desktop's reader take all
twenty-seven back and say them.  A window's list buffer is the
program's own now (`tiku_draw_open` takes it), so a small window pays
for a small buffer, and a window presents its list straight onto the
wire without a second copy.  Icon art and pictures are budgeted at two
kilobytes a window and refused past it, shown in the test.

Milestones 2 and 3 landed together.  `tiku_geom.h` carries the
rectangle and the colour with nothing that draws; `tiku_measure.h` is
the seam text is measured through, answered by the baked faces on the
desktop and the metrics table on the board; the slider, tabs, list and
palette each became a behaviour file and a `_paint.c` beside it, and
`check_kits.py` refuses a paint header in a behaviour file (shown to
fire).  `make check-behaviour` compiles every behaviour file with the
board's own compiler.  On the board, `applications.mk` compiles the
kit's behaviour and paint files in place, and the paint files draw
through a recording surface: the four kit headers that paint
(`tiku_gfx.h`, `tiku_font.h`, `tiku_ui.h`, `tiku_dl.h`) and
`tiku_event.h` open with a `TIKU_DISPLAYLESS` switch that hands the
same names to the board's recorder, since a kit file's own `#include
"..."` finds its own directory first and no include order could shadow
it.  The sample draws its tabs, list, slider, field and button with the
kit's own calls, in 277 bytes.  Two things the plan expected of this
milestone moved: the desktop's example programs building for the board
need the application runtime, which is milestone 6, and the menu and
alert widgets stay desktop-side until milestone 4 carries menus as text.

Milestone 4 carries the rest of the session.  Menus go as text, `menu
File`, `item <command> <shortcut|-> <label>`, `sep`, which the desktop's
listener expands into its own menu set (`test_menutext`), a hundred
bytes where a menu set by value is ten kilobytes; a pick on it rides
back as a command.  Raise, place, sizeable and resize are one message
each, eight windows a session.  The file picker and the clipboard cross
as the desk's own self-describing message, flattened byte by byte on the
board and read by the desktop's own reader (`test_gui`): a board asks
for a file and the chosen path returns to the window that asked.  On the
LM20, `boarddraw.script` opens the board's window, works its tabs, list,
slider and field, and picks a file from the board's own menu bar whose
path lands in the field.

One thing the console taught, again.  A board sends its window's traffic
the moment it boots, which is the moment the desktop is busiest reading
the board's namespace to mount it, and the shared console drops the
frames that collide.  So the board holds a window's open and its list
until a desktop greets it, which the desktop does after the mount, when
the line is quiet; the window is announced then, and a board that boots
after the desktop is greeted just the same.  With that the window opens
on every run.

Costs sum to about three weeks.  Milestones 0, 1 and 2 are desktop-side
refactoring with the suite as the net; 3 to 6 are where the board gains
the kit; 7 is what keeps it gained.

## 3. Decisions that are yours

1. **A recording surface on the board, or a parallel writer.**  The
   proposal gives the board a `tiku_surface_t` that only records, so the
   desktop's paint code runs on the board as it is.  The cost is that
   `tiku_ui`'s painters must be split too, so the board links the
   recorders and not the pixel painters.  The alternative keeps the
   board's generated writer and asks every program to paint twice.  The
   proposal is the one where "add once" is true.
2. **Behaviour files compiled from the desktop's tree.**  The overlay
   reads `kits/interface/*.c` in place, so there is one copy.  If the
   firmware should not reach into the desktop's sources, the same files
   can be vendored into `board/` by a make rule with a hash check, at
   the price of a second copy to keep current.
3. **Menus as text.**  Ten kilobytes by value is the desktop's own
   habit; a text form is what the board can afford and what a person
   can read.  The desktop would accept both.
4. **How much of `tiku_textview`.**  Many lines with a caret is the
   editor's heart and is already pure; it is large.  A board that wants
   a text editor gets it; one that does not need not link it.  The
   overlay lists behaviour files one by one for that reason.
5. **Fonts at sizes.**  The board measures at the desktop's baked sizes
   only.  A program that asks for a face at another size on the board
   is answered with the nearest baked one; the plan does not put a
   rasteriser on the board.
6. **Pictures and icons.**  Both cost bytes over a serial line.  The
   proposal budgets them per window and refuses past it, rather than
   making a window arrive a minute late.

## 4. What this touches and what it does not

- New: `dl_ops.txt` and its generator, `tiku_*_paint.c` for the mixed
  controls, the board's recording surface, the compact menu message,
  the board runtime, `toolkit.md`, three checks.
- Changed: `tiku_dl.c` (reads the table), `check_kits.py` (the
  behaviour rule), `applications.mk` (the behaviour files), the desktop's
  remote listener (menus as text, more windows), `tiku_draw` (grown into
  the runtime).
- Not touched: the wire's framing, the forms road, the namespace, the
  firmware outside the overlay's existing hooks, the desktop's own
  applications, which gain a third way to build and lose nothing.
