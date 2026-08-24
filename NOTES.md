# TikuTracker — build notes

## N1 (2026-08-11): model layer, headless, hardware-proven

Built and green on all three boards (51 checks on ra8p1):
- `tiku_trk_task`     TaskLoop: one-shot / periodic / run-when-idle /
                      ACCUMULATING (50 notices for one folder collapse to a
                      single refresh; the deadline slides while the storm
                      lasts; max_count stops a continuous storm deferring
                      the work forever).  17 checks.
- `tiku_ident`    scored device identity, uid=8/id=4/mcu=2/name=1,
                      bind at 7, and a uid mismatch is an ABSOLUTE VETO so a
                      renamed board cannot adopt another's arrangement.
- `tiku_state`    Tracker's AttributeStreamNode seam: xattr (local, the
                      object owns its state), sidecar (per-device file keyed
                      by devid), memory.  Survives close/reopen.  The
                      no-removal-on-teardown invariant is in the header
                      because the obvious implementation loses the user's
                      arrangement on unplug.
- `tiku_model`    Model + facts + the backend vtable.  The view asks
                      questions (is_container / is_mutable_container /
                      is_writable), never "which backend is this".
- two backends        POSIX (real perms, inodes, xattrs) and TikuOS (the
                      proven session/namespace client).

### Two real defects the hardware test caught
1. `/data` looked immutable: the manifest's perm column describes a NODE's
   read/write handlers, and a directory has none, so every device directory
   arrives "--".  On a container, WRITE must mean "entries can be created
   here" (what access(W_OK) answers locally).  Dynamic store: yes;
   namespace directory: no.
2. Files in `/data` were classified as live value nodes, which would have
   made every saved BASIC program look like a sensor reading and denied
   them file semantics.  A store's contents are FILES.

Both were predicted in spec/DECISIONS.md as the "manifest does not expose
dynops" gap.  Firmware follow-up (one line, from the reconciler): emit a
distinct type character for a dynamic directory so this stops being keyed
on the path.

### Not yet built (later phases, per the reconciled build order)
devio request queue, watch broker, fsevent synthesis, permit/verdict cache,
roster, type db, query runner.  N2 needs only what exists.

## N2 (2026-08-11): the first real window

`build/TikuTracker` — one X11 window drawn entirely by us over the pose
engine.  Runs against a local directory (`TikuTracker /path`) or a live
device (`TikuTracker -p /dev/ttyACM0 /sys`); `-shot FILE -seconds N`
captures what it drew, so the deliverable is reviewable without someone
watching it.

Working, on hardware: folders sort first; the title bar sorts on click and
reverses on re-click with the marker following; rows select with Tracker's
own rules; arrows/Home/End navigate and scroll the target into view;
type-ahead jumps or filters and shows the string in the count view; double
click opens a container; Backspace goes up; the loop pumps the device
between X events so **live values update in place** while the window stays
responsive.

`tests/test_poseview` — 33 checks, each naming the FEATURES row it proves
(PVS-001..008, PVK-002..010, type-ahead scoring and filtering, sort
toggling). This is what lets a row be marked `done` honestly.

### Two engine bugs the rule tests caught
1. An empty type-ahead string filtered the listing to NOTHING instead of
   restoring it — clearing the filter emptied the window.
2. Arrow-key seeding keyed on "no active pose" rather than "empty
   selection", so after Select None an arrow moved relative to the
   last-clicked row instead of seeding first/last (PVK-003).
(Two further failures were wrong assertions in the test itself, corrected
against the source rather than by loosening the check.)

### A firmware defect the window surfaced
`/sys/cold_boots` returns a cumulative LIFETIME-SECONDS accumulator, not a
count of cold boots — obvious once the GUI showed it beside uptime, which
it tracks exactly. The implementation and its own section comment say
"lifetime uptime accumulator"; only the node name says cold_boots. Renaming
a public namespace node is the user's call, so it is reported, not touched.

## N3 (2026-08-11): icon mode, spatial memory, rubber band, drag

Working on hardware and locally:
- icon and mini-icon modes (Ctrl-1/2/3), the column bar hidden outside list
  mode, labels truncated to the cell
- the grid exactly as specified: cell = iconSize + ceil(iconSize*0.875),
  offset = ceil(iconSize*0.625); pinning rounds to the NEAREST cell
- auto-placement left-to-right wrapping at the view edge, occupancy tested
  only in the candidate's vertical band, Ctrl-K cleans up
- locations stored UNSCALED at 32 px, so an arrangement made at one icon
  size reappears in proportion at another
- drag moves the whole selection by one delta; Ctrl snaps to the grid
- rubber band selects what it intersects; modifiers extend
- SPATIAL MEMORY, and it is the N1 design paying off: local files keep
  positions in xattrs ON THE OBJECT (user._trk/pinfo, folder view state in
  user._trk/viewstate); device nodes keep them in the per-device sidecar.
  Same code path, verified both ways -- drag icons, quit, reopen, they are
  where you left them.

tests/test_poseview: 48 checks now (15 new for the grid, placement,
scaling, band, hit-testing and the save/restore round trip).

### Two bugs the first capture caught
1. Files vanished after a reload: restore only positions what the USER
   placed, so anything auto-placed had no position and was never re-placed.
   Fixed centrally (settle_layout) -- PVL-020 says exactly this.
2. The column title bar stayed visible in icon mode, and hit-testing used
   a different rectangle from drawing (PVL-005).

### Honest gaps, measured not guessed
- The TaskLoop is built and tested but NOT yet wired into the app: the
  event loop still calls pump() directly. Wiring it is the next
  architectural step, not a rewrite.
- Loading is synchronous; Tracker spawns AddPosesTask so a huge folder
  never blocks. Our folders are small so far, and the seam is ready.
- Liveness is POLLED, not event-driven: no node monitor, no FSNotification
  pipeline, no pending-node cache. This is the biggest remaining
  architectural gap and it is what the `sub` firmware command plus the
  watch broker (spec/DECISIONS.md) are for.

## N4 (2026-08-11): file operations

`tiku_fs` — copy, move, duplicate, trash, rename, undo, run as a STEPPED
queue (tiku_fs_step) rather than a blocking call, so the window keeps
drawing while an operation runs and a collision can stop the queue and wait
for an answer instead of guessing.

Rules implemented from the contract, with the verifier's corrections:
- FS-001/002: same volume moves, different volume copies; an explicit copy
  overrides. (The Option-forces-copy row was one of the ones the verifier
  found stated BACKWARDS, so it is implemented as corrected.)
- FS-005: an item already in the destination is silently skipped, never
  copied onto itself
- FS-006: "<name> copy", "copy 2", "copy 3" — and duplicating something
  already called "copy" gives "copy 2", never "copy copy"
- FS-008: duplicate lands in each item's OWN parent, even across a
  multi-folder selection
- FS-009: a link keeps the plain name unless taken, then "<name> link"
- rename refuses "", "..", and anything containing '/' BEFORE attempting
- undo reverses in reverse order, names the operation ("Undo Move"), and
  refuses to offer itself for a delete rather than failing later
- the Trash is stripped from any destructive selection, so it cannot be
  trashed

In the window: Ctrl-D duplicate, Delete (says "no Trash — this is final"),
Ctrl-Z undo, F2 inline rename with a real editor mode that swallows the
view's keys while active. Progress and refusals show in the count view.
Verified end to end: duplicating from the window really produced
"file.txt copy" and "other.txt copy" on disk.

tests/test_fs: 30 checks. Whole suite now: 17 + 51 + 48 + 30 = 146.

### Device semantics kept honest
Rename is refused up front on namespace nodes (they have no name of their
own to change) rather than letting the device answer EACCES after the fact.

## N5 (2026-08-11): the desktop and the Deskbar

`build/TikuDesktop` — one host window holding a whole little desktop:

- `tiku_trk_win`: the window system. Windows in one array ordered back to
  front; drawing walks it forward, hit-testing walks it backward, which is
  the whole of z-order. R5 tab chrome with close and zoom widgets, drag by
  the tab (clamped so a window can never be dragged out of reach), resize
  by the bottom-right grip. NO backing store: a window redraws its content
  when asked, which is R5's own expose model and the reason this shape
  would fit a small machine too.
- Spatial: opening a folder opens ITS window, and opening it again RAISES
  that window instead of making a second one. Windows cascade so a new one
  never lands exactly on the last.
- `tiku_trk_deskbar`: the corner strip — leaf button, the open-window list
  with the focused one marked, and a tray whose replicants are live
  namespace nodes plus a clock. Clicking a window name raises it; the leaf
  menu opens /sys, /dev, /data or quits.
- One pump per cycle serves every window AND the tray, so a device is read
  once however many windows are showing it.

Proven on the RA8P1: /sys and /dev open as separate windows, and the tray
reads uptime 74248, boots 200, free 32768 live from the board.

### Still open here
Menus are the leaf menu only (no per-window menu bar yet), there is no
context menu, and windows do not yet persist their frames between runs
(the state store is ready; the frame is simply not written to it).

## Deskbar rebuilt (2026-08-11) — the first one was a replica

The first Deskbar was built from imagination while 75 extracted behaviour
rows (DB-001..DB-075) sat unread in FEATURES.md. That is the same mistake
this whole contract exists to prevent, caught by the user on sight.

Rebuilt against the rows:
- the leaf menu has the FIXED HEAD the original always shows (About this
  system / Find / preferences / separator / Shut down), with our places
  below it — not an invented list
- the application list is a TEAM list: one entry per application, with an
  icon, an expander triangle, and the application's WINDOWS NESTED
  underneath it when expanded (DB-021). One entry however many instances.
- window entries carry a state icon distinguishing shown from minimised,
  the current window is marked, and titles have the application's name
  STRIPPED off either end ("Tracker: /sys" and "/sys - Tracker" both read
  "/sys") — the row that stops the column being the same word repeated
- window list kept in NATURAL title order, so "Doc 2" precedes "Doc 10"
- right-click an application opens its window menu ending Hide all / Show
  all / Close all; Close all closes the WINDOWS, never quits the app
- Ctrl+click a window minimises instead of raising
- labels truncate with an ellipsis to the available width
- the clock shows short date AND time, and clicking it pops a calendar with
  the current day marked; the next click anywhere dismisses it
- dock modes exist as a type (right/left expanded, right mini) with mini
  collapsing the list away

Still missing versus the original: replicant drag-to-reorder, scroll arrows
when the list overruns, auto-hide, the switcher, drag-to-redock, and the
tray's horizontal wrapping (ours stacks vertically). A workflow extracting
Deskbar's exact geometry constants from BarView/StatusView/ExpandoMenuBar
is running; those numbers replace the estimates here when it lands.

## Menus, icons and the audit (2026-08-11)

Three things landed together.

**Real Haiku icons.** 34 HVIF blobs, verified byte-identical to resources in
Haiku's own tree (TrackerIcons.rdef and the MIME database) rather than taken
on trust from the generator's header comment. A from-scratch HVIF parser and
rasteriser renders them at any size; the model answers which icon a node
wears, so pose rows, icon mode and the Deskbar all ask one question. Two
parser defects an adversarial pass found were fixed: truncated blobs were
silently accepted as valid empty icons (253 of 17,542 truncations of the real
set), and a short command block was decoded from uninitialised stack.

**Menus**, ported rather than invented: geometry from the menu kit's own
layout code (the 14/2/20 padding from one label-spacing scalar, the reserved
mark gutter, the stroked check and submenu arrow, the two-line separator, the
one-pixel seam between items), and the action table from Tracker's own
Shortcuts class -- one place deciding every action's label, command, key and
enabled state, so the File menu and the pose context menu cannot disagree.
Nine actions rename themselves while Shift is held and change command with
their label; an item showing its shifted face still updates back, which needs
the shifted command to map to its base.

**The audit.** 33 done, 227 differs, 629 open. See FEATURES.md.

Deskbar metrics are now the extracted ones (134 wide, 20/19 rows with a 1 px
seam, tray gutters, stroked expander) and the selected row is a dark slab with
black text rather than a blue bar with white -- the original never changes
menu text colour on selection.

## N6: attributes and queries (2026-08-11)

The manifest already carries the piece Tracker had to consult a MIME registry
for.  Its meta column is a typed descriptor -- `vtype,unit,fresh,cost[,lo..hi]`
-- so a node states what its reading MEANS, and that one field drives all of:

- **formatting**: 32768 reads as "32.0 KiB" on a bytes node and "32.768 kHz"
  on a clock; 59182 reads as "16h 26m"; a bool reads "on"/"off".  The uptime
  in the Deskbar tray was the value that made this worth doing.
- **validation**: a bad edit is refused BEFORE it reaches the device, naming
  what it wanted ("banana" is not on or off / 500 is outside 0..100).  A
  refusal that names the range is worth more than the device's error.
- **sorting**: numbers compare as numbers, so 9 bytes sorts before 10 KiB.
- **querying**: "every node measured in bytes" is a term over the unit.

Editing a value in place is wired: Enter on a leaf edits it, Enter commits,
Escape cancels.  Proven on hardware -- /dev/led1 written to 1 from the list
and read back through the device, and "banana" refused with the value left
untouched.

The query engine is Tracker's shape: terms of (field, operator, value)
joined by and/or, with the SCOPE enforced outside the predicate as a path
prefix -- the same split, and the reason a scope can be added to a running
search.  The two rules that look odd until you read the source are both
ported: a name search is a substring UNLESS the text contains a star anywhere
(then the whole text is a pattern), and case-insensitivity is a rewrite of
the value into per-character classes ("txt" -> "[tT][xX][tT]") rather than a
comparison flag, which is also why a typed space matches any run.

Ctrl+F searches from the focused folder; results open in their own window
showing full paths, since results come from all over the namespace.

One bug the hardware found: ordering compared UNTYPED values as text, so
"30" counted as over 1000 and a device name did too.  52 spurious results
became 18 real ones.  Both members of that rule are now tested.

Names truncate in the MIDDLE with one U+2026, not with trailing dots -- the
extraction's correction, and the right one for a column of names sharing a
prefix.

NOT done: the Find panel proper (three modes, attribute rows, target menu),
live queries that update as the device changes, and query files.

## The Find panel, live queries and saved queries (2026-08-11)

**The panel** is the original's shape: a mode popup (by name / by attribute /
by formula), and in attribute mode a grid of rows, each an attribute, an
operator and a value.  Two details are ported rather than guessed:

- the and/or that joins a row to the NEXT one sits on the row ABOVE it, and
  the fold is strictly left to right with no precedence, so "a or b and c"
  means ((a or b) and c);
- the operator menu offers only what the field's type supports, and changing
  the field resets an operator the new field cannot do.

Switching into formula mode pre-fills the field with the composed predicate,
which is the panel's best feature: it is how the query language is discovered
rather than documented.  Leaving formula mode is where this DIVERGES -- the
original silently discards both the formula and the attribute rows, and the
rows are kept here instead.  Losing a half-built query to a mis-click is a
defect, not a behaviour worth reproducing.

**Live queries** follow the invariant the source is built on: the run IS the
subscription, so a caller that stops it when the first fill completes has
quietly turned liveness off.  A refresh does NOT clear the view first --
survivors keep their rows -- and the diff resolves the hazard the original
carries: survivors are removed from the snapshot as they are re-found, or a
result that never left would be deleted and re-added on every pass.  A
removal records WHY at the moment it is still knowable (the node is gone vs
it no longer qualifies); afterwards the two are indistinguishable.

Proven on hardware twice: the engine directly (led0 written, arriving marked
new), and the app's own path (led2 verified off, written, and the window
reporting "1 in, 0 out").

**Saved queries** keep the original's indirection -- a query is a thing you
save and re-open, not a command you re-type -- with the state store standing
in for the query file.  Serialised as versioned text so a saved query
outlives the build that wrote it; an unknown version is refused rather than
guessed at.  Names are the criteria plus a timestamp, sanitised.

Still absent: a saved query cannot yet be re-opened from the UI (the list and
loader exist and are tested, but nothing shows them), and there is no
dynamic-date equivalent -- the board has no wall clock the panel could ask
about "today".

## The controls did not look right, and why (2026-08-11)

Two separate causes, both mine.

**The Find panel hand-rolled its controls.** There is a shared R5 control
library (`tiku_ui_button` / `_checkbox` / `_radio` / `_textfield`) with a
measured spec, and the panel drew its own buttons and fields instead of
calling it -- flat fills with a single bevel, 20 px where the spec says 24,
and an invented "three descending lines" popup marker.  A panel whose
controls come from two different sets of rules reads as wrong even when
nothing in it is individually ugly.  Fixed by calling the library, and by
adding the control it was missing: a proper menu field, built from the same
primitives as a button (dark frame, clipped corners, two-step bevel) plus a
stroked down arrow.

**The window tab widgets were bevelled squares, so they read as checkboxes.**
The real ones are not bevelled at all.  Haiku's DefaultDecorator draws both
through one painter, `_DrawBlendedRect`: the rect inset by one, filled with a
DIAGONAL gradient from white at the top-left to the tab colour at the
bottom-right, inside a single B_DARKEN_2 outline.  And the zoom widget is TWO
such squares -- a small one at the top-left, a larger one offset down and
right by a quarter of the width -- not a square inside a square.  That is now
`tiku_ui_tab_widget` in the shared library.

Worth recording: R5-SPEC.md says of itself that the marked values and "the
exact tab geometry: inference, to be checked against an R5 screenshot before
S1 is called done".  That calibration never happened, and the tab widgets are
where it showed.  The tints and control metrics are documented-API values and
are sound; what remains uncalibrated is the tab geometry, the three (~)
colours (tab yellow, selection blue, focus blue) and the window border
proportions.

## Closing the gap (2026-08-11)

Re-audited the six areas whose implementation moved after the first audit
(menus, icons, attributes, columns, queries, pose layout).  Those six went
from 10 done / 107 differs / 324 open to **32 / 182 / 227**, and the whole
contract from 33/241/615 to **57 done / 299 differs / 533 open**.  The
re-audit also produced 94 differs rows with a concrete, small fix each,
which is the work list from here.

Closed in this pass:

- **The live menu relabelling was unreachable.**  `tiku_trk_sc_update` was
  written, tested and never called from the app, so the nine
  modifier-sensitive items only ever showed their unshifted faces.  Wired to
  both key edges -- and it has to be both, because X reports the state BEFORE
  a release, so a menu updated only on press stays stuck in its shifted face.
- **The zoom box was decorative**: hit-tested and never handled.  It is now
  the toggle it should be, and it measures the chrome FROM THE WINDOW rather
  than re-deriving the border arithmetic -- the second copy is what cut the
  last row off by exactly the difference.  The fit width also has to satisfy
  the rule that decides whether a value is painted at all, not merely make
  room for the text, or the zoomed window hides the values it just made
  space for.
- **Click-through**: the chrome activates a window, the content does not, so
  a click can reach a list in a window behind another.
- **Closing the last window ends the session**, and Control+close closes all.
- **The cascade** is one shared origin that advances and resets when it runs
  off screen, not a modulo -- which silently stacked the sixth window on the
  first.
- **Each Find field offers only the operators it can answer** ("greater than"
  is gone from a capability string) and starts on its own operator; a saved
  query's generated name describes every row and its join.

### A build trap worth remembering

`make` reported success three times while running stale binaries, because
`all:` never listed test_menu, test_attr or test_query -- so those suites
only ever rebuilt when named explicitly, and otherwise ran whatever binary
happened to survive.  A suite left out of `all` is a suite that reports
passes it did not earn.  Fixed: `TESTBINS` now holds every suite, `all`
builds them, and `make test` runs them all and fails loudly.

### Second pass: acting on the ranked list

The ranking agent produced 29 batches and, usefully, five SKIPs — one of
which overturned something implemented an hour earlier:

**CW-015's second half was wrong here.**  Tracker quits when its last window
closes because its Desktop is a window of its own that outlives the folders.
In this shell the backdrop and the Deskbar ARE the application, so quitting
on the last folder close would tear down the thing the user is looking at.
Reverted, and the reason is now in the code so it does not get re-added.

Closed in this pass:

- **Clean up in list mode destroyed icon arrangements.**  It resorted,
  cleared every placed flag and re-laid out against a degenerate grid
  (gx = gy = 0), so a stray Cmd+K while showing a list flattened an
  arrangement made in icon mode.  Refused in list mode now, with a test that
  fails without the guard.
- **Auto-placed positions were discarded on save**, so a folder re-placed its
  contents on every open and nothing ever settled.  An auto-placed icon is
  still where the icon is.
- **State was written only at exit** — a crash lost every arrangement made
  since launch.  Now written on close.
- **Mini-icon mode painted full-size art** into a short cell, which is not a
  third mode at all.
- **Arrange by, Reverse order, Clean up and Resize to fit were built, enabled
  and did nothing** — every one fell to `default: break`.
- **Kind and icon now consult the type**, case-folded, exact before
  supertype.  My first table had a bare "application/" row and claimed
  octet-stream as an Application; test_model caught it.  A generic blob is
  not a program, and a real executable is recognised by its permission bits
  before the table is consulted.

Contract: **71 done / 285 differs / 533 open**.

### Third pass: real bugs fixed, rows still open

Four genuine defects fixed this pass, and it is worth being clear that NONE
of their rows moved to done — each row asks for more than the bug:

- **A negative interval rendered as its opposite.**  Dividing a negative
  total truncates every field toward zero, so -3600 came out "0s" -- an hour
  ago shown as now -- and -90 came out "-30s".  Split the magnitude, carry
  the sign.  MA-035 still wants H:MM:SS where we render "1h 30m 5s", so the
  row stays *differs*.
- **A size could not be typed with its prefix.**  "64k" is how the number is
  spoken; it is now accepted and the device is sent 65536.  MA-057 also wants
  free-form dates and a char attribute that refuses multi-byte glyphs, so:
  still *differs*.
- **A '[' with no ']' was treated as a class**, so the pattern "[abc" matched
  "a" -- the scan ran to the terminator and the pattern was then considered
  spent.  It is a literal bracket now.  Q-049 also wants UTF-8-aware glyph
  stepping and 100-level backtracking: still *differs*.
- **The type-ahead filter was never rebuilt when the listing was replaced**,
  so switching folder while filtering showed the PREVIOUS folder's rows
  inside the new one -- measured: 4 rows, one real and three stale.  PVL-083
  is ClearPoses in full (saving positions first, wiping the zombie and
  broken-link records, resetting extent and hint), so: still *differs*.

That gap between "bug fixed" and "row matches" is the contract doing its job.
A count that moved every time something was improved would not be measuring
fidelity.

Three process notes from this pass, all the same shape -- I asserted instead
of measuring:

- a probe binary was stale because `||` short-circuited its rebuild, so a
  fixed bug still looked broken;
- an edit anchored on the wrong following symbol landed in a different
  function, and I only found out because the behaviour did not change;
- a "prove the guard fires" run showed nothing failing because the removal
  pattern no longer matched the re-applied fix.  The check that finally
  worked removes the code by LOCATING THE FUNCTION and asserts the removal
  matched.

## PVN: node monitoring (2026-08-11)

**0 done / 7 differs / 64 open  ->  13 / 19 / 39.**

The area's first row sets the gate: the device posts TIKU_EVENT_VFS with no
opcode -- "touched, re-read" -- so either the watch record grows created /
removed / moved / changed + name, or the view diffs a re-listing.  The device
side is worse than that row assumed: `watch` is per-node, has eight slots,
occupies the shell as a MODE while it runs, and cannot cover a folder at all.
So the notices are derived here, which is the alternative the row offers.

What matters is that the work went into the HANDLER, not the transport.
There is now one entry point every notice passes through, and the rules live
behind it: a creation in another folder adds no row; a duplicate creation
adds no second row; a removal takes the selection, the active pose and the
pivot with it and pulls a scrolled view up rather than leaving it past the
end; a rename re-reads every column and returns the row to its sorted place
without re-sorting the folder, keeping its selection; renaming the folder on
screen retitles the window; deleting it closes the window -- except the root,
which stays.  A notice for a row that does not exist yet is PARKED and
replayed in arrival order when the row appears, so a file changed the instant
after it was created shows the change and not the state it was born with.
When the device can push, only the source changes.

### Two things this cannot reach, and why

**No push channel.**  Everything is derived by re-listing on a tick.  The
device-side change that would collapse PVN-001/005/007/008/009 onto one real
source: subscribe to a DIRECTORY, push {opcode, name, old name} over the
session channel instead of occupying the shell, with more than eight slots.

**No stable node identity on the device.**  tk_from_node() synthesises
node_id as an FNV hash of the PATH (backend_tiku.c:136-140), so a renamed
device node is a DIFFERENT node to the differ and a rename degrades to
remove-plus-create -- losing the selection and the arrangement, which is
exactly what the move case exists to prevent.  It works on local files
because those carry a real inode, and that is why the test passed: the test
uses the posix backend.  The same "only covered the case it was written for"
trap the earlier skeptic pass named, caught this time by the audit rather
than by me.

Closing PVN properly needs the device to answer two questions it currently
cannot: "what changed" and "is this the same node".

## The device learned to answer both questions (2026-08-11)

PVN could not be closed because the namespace could not say WHAT changed, nor
whether two sightings were the SAME node.  Both are now answered in mainline
TikuOS, and both turned out small because the kernel already had the concept
and simply never exposed it.

**Identity.**  tiku_vfs.h already said it: "the tree is static, so node
addresses are stable for the life of the system" -- the kernel uses the node
pointer as its own subscription key.  The manifest just never carried it.  It
now has an `id` column (schema rev 3), and tiku_vfs_node_id() hands the same
token out as an opaque value.  The host had been synthesising an id by
hashing the PATH, which is why a rename looked like a different node; it now
takes the device's and falls back to the hash only for an older board.

**Change records.**  A ring of {node, op, seq}, drained by READING
/sys/vfs/events -- a node, not a shell mode, so an agent needs no new
protocol and the shell stays free.  Making tiku_vfs_notify() the CHANGED case
of the opcode form is what fills it: every existing caller -- shell writes,
BASIC writes, network writes, driver notifies -- now records what it did
without one of them being touched.  A full ring drops the OLDEST and counts
it, and the drain reports the count, because a reader that is told its
picture is incomplete can re-read, while an ISR that blocks cannot recover.

TikuBench suite `vfs-events`, 11 checks, 11/11 on the RA8P1: 175 nodes each
with a stable unique id; one write -> one `changed` record carrying that same
id; sequence advancing; reads recording nothing; and the overflow path
dropping loudly.

Three things this cost, all the same shape as before:

- I wrote a comment saying the record was routed through tiku_vfs_notify()
  and did not write the code.  The board reported zero pending records, which
  is the only reason it was caught.
- TikuBench ran against a DIFFERENT board -- three TikuOS devices are
  attached and it auto-selected one still on rev 2.  The failure read exactly
  like a broken feature.
- The host parser read the new column without stepping over its separator,
  so it silently got an empty string and fell back to the hash -- every other
  field in that loop has the step, mine did not.

## PVN, and what the push channel actually took

The device now answers both questions (manifest rev 3 `id`, /sys/vfs/events
ring, TikuBench `vfs-events` 11/11 on the RA8P1), and the host uses them:
tk_list re-lists live instead of serving the connect-time manifest, the
backend takes the device's identity, and the view asks "what changed" before
re-reading anything.

Two bugs in that wiring, both caught by audit rather than by me, both of a
kind my hardware check could not have found:

- **The op was thrown away.**  A record naming a row the view HOLDS was
  scored as handled whatever had happened to it, so a removal or a rename
  suppressed the very re-read that would have noticed -- the deleted row
  stayed on screen.  My hardware check exercised a value change, the one op
  that happens to work that way.
- **One destructive ring, N windows.**  Reading the ring TAKES the records.
  Every view drained for itself, so the first window consumed everyone
  else's and the rest were told nothing had happened: liveness silently lost,
  which is the exact failure the row about slot exhaustion exists to prevent.
  The drain now happens once per tick, at the desktop, and the records are
  handed to every view.

Both now have tests that fail without the fix, and both are proven on
hardware: a quiet tick over two windows costs one question and does no work;
a write to /dev/led1 marks that row stale in BOTH windows from a single
drain.

The remaining PVN rows are not blocked on the device any more.  They are
Tracker mechanisms this port has no counterpart for yet -- a zombie tier that
holds an unreadable entry back rather than showing it as unknown, a pose-info
visibility bit, symlink-target and volume freshness, a broken-link state, and
a viewport-relative scroll rule that needs the view to know its own height.

## PVN, finished: 2 done / 35 differs / 34 open

Two audits and two skeptic passes. The skeptic overturned 20 of 21, then 12
of 14. The number above is the demoted one both times; no flattering figure
is recorded anywhere in this file.

The structural work is real and was verified by the auditors' own probes, not
by my say-so:

- parking is no longer dead code -- a change record for a node the view does
  not hold becomes a real ATTR event via the new path_of_id op, is parked and
  is replayed with its value beating the later listing;
- the MOVED move-out branch now leaves by the shared tail, so a filtered view
  really loses the row (the auditor reproduced the discriminating result);
- the broken-link TRANSITION works on posix -- a live link whose target is
  deleted keeps its row and flips to "Broken link";
- the anchors are one index space; the scroll rule counts the displayed list.

WHY IT IS STILL 2, and this is the honest core of it: **most of these rules
have no producer.** The zombie hold is correct in both paths and neither
backend ever emits KIND_UNKNOWN, so nothing can reach it. The overflow
fallback is correct and cannot trigger -- the firmware ring holds 16 records
and the drain buffer takes 32. The link_broken term in facts_differ is inert:
removing it changes no test. The folder-GONE branch cannot fire on the device
because the manifest is cached at connect and never refreshed, so a deleted
folder returns count 0 rather than an error.

That is the shape of the remaining work, and it is not more handler code: it
is giving these rules something that can actually produce their condition --
a manifest that refreshes, a backend that can say "I could not describe
this", a drain smaller than the ring. Writing more mechanism without that is
writing more code nothing reaches, which is what the last two passes did.

The DEVICE work stands on its own and is independent of all of it: manifest
rev 3 identity and the /sys/vfs/events ring are in mainline TikuOS, flashed
on the RA8P1, TikuBench vfs-events 11/11.

## PVN closed, skeptic-verified: 23 done / 16 differs / 32 open

Three audit+skeptic rounds.  The first two skeptics overturned 20/21 and
12/14; this one sampled 27 and overturned TWO -- and ran the suites and built
its own probe before agreeing.  The difference was not more mechanism; it was
PRODUCERS.  A rule is only real when something can produce its condition:

- tk_stat now asks the LIVE device about a path the manifest lacks, and a
  node its parent lists but cannot read is KIND_UNKNOWN -- the producer the
  zombie tier was missing.  The posix producer (unreadable dirents) was
  verified by the skeptic's own probe.
- tk_changes reloads the manifest when a structural record arrives, so a
  gone node is detectable on-device.
- a held CHANGED goes through the ONE handler as a re-stat'ed ATTR event;
  the inline value poke -- the second update path -- is gone.
- the created-dedupe matches path, node identity AND the zombie tier.
- a rename keeps its scroll; insert re-establishes sortedness when a value
  refresh under a value sort broke it; a bare reload filters hidden rows.
- a full park now yields its OLDEST notice, as the device ring does.

The two the skeptic took back: the device UNKNOWN producer's exact claim was
overbroad (PVN-014's device leg), and zombie promotion-by-record has no live
producer (PVN-037).  Both recorded as differs, not argued with.

The 32 open are Haiku mechanisms without objects here: per-entry watch masks
(no watches), mount events (one fixed backend), MIME database, volume poses,
Trash icon, Open-With, query-file deletion.  Each needs its subsystem first;
none is a node-monitoring defect.

Deskbar's Find item now opens the panel (it was drawn and unreachable), and
the Find window is non-resizable (Q-001).
