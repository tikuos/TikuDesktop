# A board's interface on the desktop

*A plan for TikuGUI: a library a TikuOS process on a board uses to put a
window on TikuDesktop and hear what is pressed in it.  2026-09-05.*

## 1. Is it possible

Yes, and most of the road is already laid.  Three things settle its shape:

- **The desktop already draws a panel a device describes.**  A `.panel`
  file is six kinds of row in plain text (`title`, `text`, `gauge`,
  `toggle`, `field`, `button`), each bound to a path; the desktop reads
  it through the namespace, draws it in its own face, and a press writes
  the bound place (`kits/interface/tiku_form.h`, `forms.md`,
  `tiku_trk_desktop_panels.c`, `devicepanel.script`).  A board mounted
  over its console is a namespace like any other: `ls`, `cat`, `write`,
  `sub add`, and `~path` lines pushed when a watched place changes
  (`kits/device/tiku_ns.h`, `tiku_session.h`).  The panel is the whole
  thesis in one place: the board publishes a few lines of text and the
  desktop is the program that draws them.
- **The board has the other half in pieces.**  A process is a
  protothread that receives events; the VFS has read and write handlers,
  `tiku_vfs_notify()` for a value that changed, a change ring the host
  drains; the shell's `watch` streams values while it stays interactive.
  What the board has no word for is a *control*: nothing on it says "this
  place is a toggle labelled Lamp", and nothing turns a write into an
  event for the process that owns the place.
- **The overlay contract is the seam.**  `applications.mk` is included
  by the firmware's Makefile and may add sources and `-DTIKU_APPL_*`
  macros, never patch mainline; a hook mainline needs sits behind that
  macro.  The contract has no call sites yet; this library is its first.

So the library is two thin ends of one existing road.  On the board,
`tiku_gui` lets a process declare a form of up to sixteen rows bound to
its own variables and callbacks, and publishes it as a `/gui/<form>/`
subtree: a `panel` node the desktop already knows how to draw, and one
node per row whose write becomes `TIKU_EVENT_GUI` to the process.  On the
desktop, the described panel grows into what a board needs: opened when
the board asks rather than only on a double-click, several at a time,
rows that update as the board's values move, rows a person can type in,
and presses that never stall the desktop on a slow cable.

What this deliberately is not, at first, is a drawing protocol.  The
window session (`kits/application/tiku_remote.h`) can carry a display
list over any file descriptor and would let a board draw anything, but
it needs a framed channel multiplexed onto the console, a display-list
emitter that does not drag the font tables onto the board, and flow
control the console does not have.  That is the last milestone, taken
once the forms road has proved the loop end to end on the LM20.

## 2. Milestones

| # | Milestone | Scope | Cost | Done when |
|---|---|---|---|---|
| **0 ✓** | The seam | `applications/board/gui/tiku_gui.h/.c` and an `APPL_SRCS` block in `applications.mk` behind `TIKU_APPL_GUI` (default off); the one mainline hook, a fifth root entry `/gui` in `kernel/vfs/tiku_vfs_tree.c` inside `#if TIKU_APPL_GUI`, filled by `tiku_gui_vfs_get()`; a board sample process `applications/board/samples/panel_demo.c` (a gauge fed by a timer, a toggle on the LED, a button that counts, a text row) started from the init table (`start gui-demo`); the flash-and-talk recipe for the LM20 written down | half a day | `make MCU=nrf54lm20a TIKU_SHELL_ENABLE=1 TIKU_APPL_GUI=1` builds and boots on the LM20 with `ls /gui` answering; the build without the macro has no `tiku_gui` symbol (`nm`), not merely the same size |
| **1 ✓** | A form on the board | the API: `TIKU_GUI_FORM(demo, "Board one")`, then `tiku_gui_text/gauge/toggle/field/button(&demo, "Label", &variable, ...)` binding each row to the process's own storage, names at most 15 characters so `/gui/<form>/<row>` fits the 64-byte path; `/gui/<form>/panel` reads as the desktop's form text, with bind paths as the board's own paths; each row node reads the variable and a write parses it, stores it, and posts `TIKU_EVENT_GUI` (data = the row) to the owning process, so the process's callback runs in its own context, never in the shell's; `tiku_gui_changed(&row)` notifies the VFS; static tables, `TIKU_GUI_FORMS` 2 × 16 rows; `test_gui.c` in TikuBench's manner on the host build of the same file | one day | on the board, `cat /gui/demo/panel` prints the rows, `write /gui/demo/lamp 1` lights the LED and `cat /gui/demo/presses` shows the callback ran; the host unit test covers parse, bounds and refusals |
| **2 ✓** | The desktop opens it | a `panel` node under a mount's `gui/` is a described panel: the Deskbar's board row lists the board's forms, and a `~gui/<form>/panel` notice opens or refreshes it, so a process that starts can say "show me" by notifying its own panel; bind paths in a board's panel are resolved against that board's mount, since the board cannot know where it was mounted; up to `TIKU_TRK_PANELS_MAX` (4) panels open at once, keyed by path; a refused write shows in the panel's status line instead of being dropped | one day | `boardpanel.script` (`# board: yes`) on the LM20: pick the form from the Deskbar, `expect-title Board one`, click the toggle, `expect-narrate` the LED row on, `read` the presses node through the mount and see 1; the same script passes against the folder-backed fixture with no board |
| **3** | Live values | the panel subscribes its bound rows through the mount (`sub add`, `~` lines) and refreshes only rows the notice pump heard about; on firmware without push, the rows poll at the mount's tier interval; on the board, `tiku_gui_changed()` coalesces into the change ring rather than one watch slot per row, so a form of sixteen rows does not exhaust the eight slots; a row that goes quiet greys rather than showing a stale value | one day | the sample's gauge, driven by a timer on the board, moves on the desktop within a second with nothing pressed (`expect-narrate` twice with `wait` between); the link stays under a quarter of its bandwidth at 115200, measured by the mount's counters |
| **4** | Input rows | three kinds the form lacks: `entry` (click focuses, keys edit, Enter writes; Escape reverts), `slider Label path lo hi` (the volume knob's track and knob, writing on release), `choice Label path a|b|c` (a menu field); the board library gains the matching rows and typed setters; the form's unknown-row rule keeps an older desktop drawing what it can | one to two days | typing a name into the sample's entry lands in the board's buffer and the process's callback sees it; the slider writes once per release, not per move; `test_form.c` and `boardpanel.script` cover all three |
| **5** | The slow cable | writes from a panel go through a queue drained by the notice pump, never on the event thread; the panel's write timeout is two seconds, not the mount's twenty; a board that stops answering greys its panels and says so in the status line; a cable replugged reopens the port (`tiku_tx_reopen`) and refreshes; the BEEP moment on a refused write | one day | pulling the LM20's cable mid-session leaves the desktop answering with the panel greyed; plugging it back in refreshes the panel with no restart; `boardpanel.script` gains the refused-write case against the folder fixture |
| **6** | The drawing road | a framed channel on the console alongside the shell (a `0xC0`-delimited frame with a channel byte, on the pattern of the SLIP demux, host side in `tiku_session`), the desktop adopting the channel as a window session (`tiku_remote_adopt`) with `-session` on a mount; a direct display-list emitter for the board (`tiku_dl` append calls with a baked metrics table, no surface, no font rasteriser); window-sized flow control on the `.`-ack precedent; the hello example built for the board | a week, after 0–5 hold | the board's hello window is drawn on the desktop from a display list of a few hundred bytes and its key presses come back to the board's process; the console shell stays usable on the same cable throughout |

### Where it stands (2026-09-05)

Milestones 0 through 2 are built and proven on the nRF54LM20A over its
console at `/dev/ttyACM0`.  A board process declares a form; `/gui/<form>/
panel` reads as the desktop's form text and each row's node reads and
writes the process's own storage, posting `TIKU_EVENT_GUI` to it.  On the
desktop the described panel became a set of up to four, opened from the
board's Deskbar row or unasked when the board first rings its `/gui`, with
binds resolved against the mount.  The sample form opens itself on the
desktop and shows the board's live values; the folder-backed
`devicepanel.script` and `boardpanel.script` both pass.

One limitation is now measured and belongs to milestone 5.  The console
has no flow control, so a control WRITE sent while the desktop is polling
the board can lose bytes off its tail: the board reads `write /p` without
the value and answers `Usage:`.  The device backend now treats that as a
failure rather than a silent success and retries, but a saturated link
still drops all three tries.  Reads are reliable; a value written back
over a busy console is not, until milestone 5 gives the write its own
paced, checked path (or milestone 6 a framed channel).  The board-side
loop itself is sound: a direct `write /gui/demo/lamp 1` lights the LED and
the process's callback records it, every time.

## 3. Decisions that are yours

1. **Forms first, drawing later.**  The proposal puts the loop through
   the described panel because every piece of it exists and the cost of
   a control is a line of text.  Starting from the window session would
   give a board arbitrary drawing on day one, at the price of the whole
   of milestone 6 before anything is seen.
2. **Where the forms live.**  `/gui` as a fifth root entry, behind the
   macro, reads well in `ls /` and keeps the board's own paths short.
   The alternative is `/proc/<pid>/gui`, which ties a form to its
   process for free but makes every bind path longer and rebuilds with
   the process table.
3. **How the board asks to be shown.**  A notice on the panel node, so
   "show my interface" is the same act as "my interface changed" and
   nothing new crosses the cable.  A Deskbar row alone would leave the
   board unable to ask.
4. **The API's voice.**  Events to the process with the row as the
   payload, and an optional callback the library calls from that event;
   never a callback from the shell's context, which is where the write
   arrives.  This follows the protothread rule that a process's state
   changes only in its own thread.
5. **The name.**  `tiku_gui` on the board and `TIKU_APPL_GUI` for the
   overlay macro, so the desktop's `tiku_ui` (the toolkit) and the
   board's library are not confused for one another.
6. **How many rows and forms.**  Sixteen rows is the form's limit and
   two forms is enough for a sample; both are compile-time constants a
   board with more SRAM can raise.

## 4. What this touches and what it does not

- New: `applications/board/gui/` (the library), `applications/board/
  samples/panel_demo.c`, the `TIKU_APPL_GUI` block in `applications.mk`,
  `boardpanel.script` with a folder-backed fixture, `test_gui.c`.
- Changed on the desktop: the described panel (several at once, opened
  by notice, subscribed rows, a status line, queued writes), the form
  (three row kinds), the Deskbar's board row, the notice pump.
- Changed in the firmware: one guarded root slot in `tiku_vfs_tree.c`.
  With the overlay absent the firmware is byte-identical to today.
- Not touched: the shell and its commands, the drivers, the device kit's
  wire vocabulary, the Tracker browser shell, the window session until
  milestone 6.
