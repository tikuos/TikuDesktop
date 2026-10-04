# One toolkit, both ends

The desktop's interface kit and a TikuOS board draw with the same code.
A control is written once; it reaches the board the same day.  This is
how the two ends are kept the same, and how to add to them.

## The three seams

1. **The op table.**  `dl_ops.txt` names every display-list op with its
   fields in wire order.  `gendl.py` writes `tiku_dl_ops.h`, which the
   desktop's reader (`tiku_dl.c`) takes its numbers and lengths from, and
   `board/gui/tiku_draw_ops.h`, the board's writers.  A row added reaches
   both; `make check-dl` fails if either generated header is stale.

2. **Behaviour apart from paint.**  A control keeps state and takes input
   in `tiku_<x>.c`, which includes nothing that draws, and paints in
   `tiku_<x>_paint.c`.  `check_kits.py` refuses a paint header
   (`tiku_gfx.h`, `tiku_font.h`, `tiku_ui.h`, `tiku_dl.h`) in a behaviour
   file, and `make check-behaviour` compiles every behaviour file with
   the board's own compiler.  Text is measured through `tiku_measure.h`,
   which the desktop answers from its faces and the board from
   `tiku_metrics.h` (`make check-metrics`).

3. **The board records instead of painting.**  Under `TIKU_DISPLAYLESS`
   the four painting kit headers hand their names to the board's
   recording surface (`board/gui/tiku_gfx.h` and friends), so the paint
   files run on the board unchanged and each `tiku_ui_*` call writes its
   op rather than pixels.

`make check-toolkit` runs all of the above at once.

The shared [dialog, dropdown and field-input controls](controls.md) compose
existing paint operations, so they do not introduce a new display-list opcode.
Their sources, along with preferences layout, property-table and text-layout
helpers, are included in both desktop and board builds. `check-behaviour` also
compiles these adapters and their painters with `TIKU_DISPLAYLESS` and the actual
board headers, catching recording-surface and event-type drift.

The shared [text-input adapter](text-input.md) assembles bounded UTF-8 commits
without changing the event structure. The desktop X11 host supplies XIM/Compose
input; fields and documents validate a whole commit before editing.

The [sensor widgets](sensors.md) add bounded histories, time-series plots,
readouts, level meters and sparklines. Their painters compose existing drawing
operations and are compiled against both desktop and board headers.

## Adding a control

Say the desktop gains a `dial`.

1. **The op.**  Add a row to `dl_ops.txt` with its fields in wire order,
   pick the next free number, and run `make dl`.  Both ends now have
   `tiku_dl_dial` / `tiku_draw_dial`.
2. **The paint.**  Add `tiku_dl_dial` to `tiku_dl.c`'s recorder and its
   reader, and paint it in `tiku_ui.c` (desktop) recording that op.  A
   control with behaviour of its own gets `tiku_dial.c` (state and input,
   no drawing) and `tiku_dial_paint.c` (the drawing), and its name goes
   in `check_kits.py`'s `BEHAVIOUR` list and `applications.mk`.
3. **The narration.**  Give the op a fact in `tiku_dl.c`'s reader and a
   word in `tiku_dl_say`, so a reader hears it.
4. **Nothing under `board/`.**  The board gains the writer from the
   table and the paint from the kit; there is no board copy to keep.

`make check-toolkit` fails until steps 1 and 2 agree, so the two ends
cannot drift apart unnoticed.

## What stays on the desktop

The font rasteriser, the icon renderer, theming and the file panel are
the drawing end's, by design: the board says what to draw in a few
hundred bytes and never carries the machinery that turns it into pixels.

### Short interaction animations

`tiku_motion.h` supplies desktop-side timing, an expanding icon outline,
and popup fade helpers. It does not own an event loop or delay an action.
Start a `tiku_motion_t` with a monotonic microsecond timestamp, then call
`tiku_motion_progress()` when drawing. A zero timestamp disables motion;
completion, cancellation, or a backwards timestamp makes the state idle.

Repaint the underlying content before drawing an outline. Request another
frame only while `motion.active` is true, and include the old outline bounds
when replacing or cancelling it. The final repaint removes the previous
outline. `tiku_motion_outline_bounds()` includes its maximum expansion.
Popup fades capture only the intersection of the popup and the repaint
clip. If capture fails, the menu still draws and accepts input immediately.

TikuDesktop and TikuTracker play a 170 ms outline when an icon is opened,
and TikuDesktop fades its menus in over 100 ms. Selection alone does not
animate. **Settings > TikuDesktop preferences > Reduce motion** turns these
effects and the workspace's window transitions off, and keeps hover
highlights and pressed controls. They are drawn on the desktop side: a
board's display list carries none of them.
