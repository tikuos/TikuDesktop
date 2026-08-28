# TikuBasic — from an MVP page to a proper editor

> **Status: PLAN, not built.**  Written 2026-08-27 against the
> applications repository at `c7c62de`.  Every claim about the tree
> below was verified on that date, by reading the code named.  The
> executing agent re-verifies line numbers before leaning on them;
> the FACTS will hold longer than the lines.

TikuBasic opens files, colours them, types the next line number, runs
a program and links its errors.  That is a working MVP and it is not
an editor: the caret cannot be moved with the arrow keys, a click on
the text moves nothing, there is no selection, no clipboard, no undo,
no find, and no scrollbar on the page.  This plan grows it into a
proper editor for writing BASIC on TikuOS, in phases another agent can
execute one by one, each ending green and committed.

## 0. Context the executing agent must have

**Repos.**  ONE applications repository now, rooted at `applications/`
(remote `tikuos/tracker.git`, private, branch `main`), holding
`TikuTracker/`, `TikuDesktop/` and `kits/`.  The tikuOS repository
above it is the OS alone (branch `vfs-watch`) and nothing in this plan
touches it.  Never `git add -f` applications files anywhere else.

**Commits.**  Author `Ambuj Varshney <ambuj.varshney@proton.me>`,
never any assistant attribution.  Subject `Kits:` / `Interface kit:` /
`TikuDesktop:` / `TikuTracker:`, then up to five one-line `- ` bullets.

**Method, per phase.**  Proving script FIRST → implement → mutation
(break the code on purpose, confirm the NAMED assert fails, restore)
→ full suite → commit.  A mutation that survives means the proof does
not prove; strengthen the script before trusting it (the cold start
once passed with file creation disabled, because a later save wrote
the file into being — the fix was asserting through the Open panel
BEFORE the first save).

**Builds.**  `make` in `applications/TikuDesktop` is THE user-facing
rebuild; four TikuDesktop binaries exist (`TikuTracker/build{,/dev}`
and `TikuDesktop/build{,/dev}`) and the person launches the
TikuDesktop-side ones — `TikuDesktop -version` says the build stamp.
The app builds as `make build/apps/programming/TikuBasic.so` and
`TikuC.so` (same `apps/tiku_ide.c`, `-DTIKU_IDE_C`); the Makefile is
NOT a prerequisite of the `.so`, so after changing flags,
`touch apps/tiku_ide.c`.

**Suite.**  Scripts in `TikuTracker/tests/shell/`;
`TRK_SHELL_ONLY=x.script tests/shell/run.sh` runs one, the bare run is
~9 minutes and must end `shell scripts hold in both front-ends`;
`TRK_SHELL_FROM=name.script` resumes a cut-short run.  Unit tests are
the `test_*` binaries the TikuTracker Makefile's `test` target lists.

**Script craft, learned the hard way.**  Typed letters go as the
lowercase key with shift (`key h shift`) — the text field carries the
case.  Double-clicks race a 500ms window; prefer click + `key Return`.
`expect-file-has` reads only the FIRST 512 BYTES.  An open strip menu
swallows the next click — leave it by picking a harmless row, never
Escape.  Kill strays with `pkill -x TikuDesktop`, never `-f`.  Probe
pixels with the `shot /tmp/x.png` op, then crop/zoom with PIL and
look.  At 1000×680 with factory prefs, today: first editor frame
(185,229,630,451), page text starts at x≈190, caret after the seeded
"10 " sits at (216,296); toolbar Save (205,268); strip menus Tiku
20 8, Project 71 8, File 132 8, Window 185 8; deskbar leaf 980 30.
RE-PROBE every coordinate at authoring time; inherit none blindly.

**Fixtures.**  `run.sh` plants per-script; `idenew.script` is the cold
start with NOTHING planted and stays that way.  New editor scripts
PLANT a project (creation is already proven) and prove EDITING.

## 1. What stands today (verified)

- The document engine is `kits/interface/tiku_textview.{h,c}` (414
  lines): owned line array (4000 lines × 1024 chars), caret, top row,
  modified flag; insert/newline/backspace/delete; moves LEFT RIGHT UP
  DOWN HOME END; `reveal`/`scroll_to`; serialize.  NO selection, NO
  clipboard, NO undo, NO page or word moves.
- The IDE's editor window (`apps/tiku_ide.c`) opens at 620×420,
  resizes, paints a toolbar (Save floppy; Run triangle for BASIC), the
  syntax-coloured page, a measured caret (the face is PROPORTIONAL —
  columns are found by measuring prefixes, not multiplying), and a
  bottom strip `name (changed) N lines`.  Per-window menu: `File >
  Save <name>` (+ Run for BASIC).
- The IDE's key road takes ONLY Return (which types the next BASIC
  number), Backspace, and printable ASCII (~:2032).  The kit's move
  API is never called: no arrows, no Home/End, no forward Delete.  The
  SIMPLE editor (`apps/tiku_edit.c` ~:692) already wires arrows and
  Home/End and reveals after every key — the IDE's page is behind the
  editor it was meant to outgrow.
- A pointer down on the page reaches only the two toolbar buttons; a
  click on the text moves nothing (~:2003).  `TIKU_EVENT_WHEEL` exists
  (`kits/interface/tiku_event.h:26`) but no editor listens, and
  whether both lanes DELIVER wheel to an application's window is
  UNPROVEN — the first phase that wants it proves it first.
- Typing past the page bottom leaves the caret off-screen: `reveal`
  is called only by `message_follow` (~:1545).
- Nothing anywhere holds TEXT on a clipboard — Tracker's clipboard
  (`tiku_trk_clip.h`) holds file poses, not characters.

## 2. The position

Keyboard-first, and the page IS the document: every gesture answers at
the caret, the menus say what the keys do, and nothing modal stands
between the person and the text.  The split rule for every phase:
whatever the SIMPLE editor would also want — moves, selection,
clipboard entry points, undo — belongs to `tiku_textview` in the
interface kit; whatever is BASIC-shaped — numbers, renumber, run —
belongs to the app.  One source, two personalities: TikuC inherits
everything below except the BASIC-only roads, which stay gated on
`e->lang == TIKU_SYNTAX_BASIC` exactly as Run and the auto-number are.

## 3. Phases, in order, each ending green and committed

- **P1 — the caret goes everywhere.**  DONE 2026-08-27.  The IDE's key
  road now wires LEFT RIGHT UP DOWN HOME END DELETE PGUP PGDN and calls
  `reveal` after EVERY key; a pointer down on the page places the caret
  — row from y, column by `edit_place_from_xy` walking the line's prefix
  widths through the face, the SAME arithmetic the caret painter runs,
  so the caret lands exactly where it will be drawn.  `ideedit.script`
  proves each move where it can only be proven — by typing a mark and
  reading back WHERE in the file it landed — plus a click landing at
  column zero of a known line, and a caret-pixel for reveal.  Three
  decisions worth keeping:
  (a) NO kit change.  Paging stayed inline (caret + `place`, mirroring
  the simple editor): a screenful is a VIEW quantity the app already
  holds as `rows`, not document state, so `tiku_textview_page` earned
  no place in the kit.
  (b) WHEEL deferred to its own phase (now P1b).  The spike was
  answered by READING, not a script: the shell consumes button 4/5 for
  its OWN windows (deskbar, folders) and drops it when the window under
  the pointer is an application window — `folder_of` returns NULL and
  the branch just `break`s.  No forwarding path exists in EITHER lane,
  so wheel-to-app is a shell+wire change, not app work, and does not
  belong in a phase that touches only the app.
  (c) A real defect fixed alongside: `edit_open` placed the caret at
  the end of the file but never revealed it, so a long BASIC program
  opened with its caret lost below the bottom edge until the first
  keypress.  `edit_open` now reveals on open — which also makes the
  reveal proof DECISIVE, because the view starts at the bottom and only
  reveal can bring the caret back to the top.
  Mutations proven by name: no-Up fails the movement marks; no key-road
  reveal turns the caret pixel to background; a pointer-ignoring place
  fails the column-zero mark.
- **P1b — the wheel scrolls the page.**  DONE 2026-08-27.  Over an
  application window the shell now builds a `TIKU_EVENT_WHEEL` (dy = ±3
  lines, button 4 up / 5 down) in window coordinates and delivers it --
  `embed_deliver` in this process, `tiku_remote_event` across the wire.
  NO wire change was needed: `tiku_remote_event` already copies the
  WHOLE `tiku_event_t` (type and dy included) and `tiku_remote_read`
  reads it back, so a WHEEL crosses intact.  The editor scrolls its
  page by `tiku_textview_scroll_to`, leaving the caret where it is.
  Proven in BOTH lanes with distinct code: `twowin.script` (EMBEDDED)
  records each pane's wheel and proves per-window delivery AND
  direction (`wheel -3` on the pane rolled up, `wheel 3` on the other
  rolled down, the untouched pane `wheel 0`); `idewheel.script` (REMOTE
  TikuBasic through the runner) rolls the wheel over the page and the
  scrollbar thumb walks from bottom to top.  A script drives the wheel
  as `press X Y 4`/`5` -- the adapter carries the button through as a
  POINTER_DOWN, which is exactly what a real wheel is.  Mutations by
  name: the shell dropping the wheel fails BOTH lanes; the editor
  ignoring it fails only the remote thumb-move, isolating the two.
- **P2 — the page says where it is.**  DONE 2026-08-27.  A real
  `tiku_ui_scrollbar` sits in a 12px gutter (`PAGE_SB`) at the page's
  right -- the same bar, colours and paging the panels and folders
  wear, so there is one scrollbar to learn, not two.  A press on the
  track pages the view (`tiku_textview_scroll_to`) WITHOUT moving the
  caret; the thumb reflects top/rows/total.  The strip gained
  `line L, col C` and, for a BASIC line wearing a number, `[N]` -- the
  number the interpreter will name it by (parsed with `atoi`, which is
  0 for a line that wears none).  The dirty `(changed)` mark stays in
  the strip.  `idescroll.script` proves it: a status glyph in the
  new-only middle of the strip (000000 where the old short status left
  background), the thumb at the BOTTOM on open (reveal-on-open), and a
  track press high on the bar that walks the thumb to the top.
  Mutations by name: no bar (gutter goes white); no paging (thumb does
  not move on the press); the old short status (the middle glyph goes
  to background).
- **P3 — selection.**  DONE 2026-08-28.  Kit: `tiku_textview` gained an
  anchor (`ay,ax`) and a `selecting` flag; `select_begin` drops the
  anchor once, `select_none` lets it go, `select_all` takes the whole
  document, and `tiku_textview_selection()` answers a NORMALISED span.
  insert / newline / backspace / delete all call a private `sel_delete`
  FIRST -- so typing, a return, or either rubbing-out key replaces what
  is selected; the deletion is counted as characters plus the line
  breaks it crosses and taken by the ordinary backspace, so there is no
  second copy of the fold logic.  App (IDE): a move with Shift extends,
  a move without lets go; Cmd-A selects all (caught before the switch
  so its letter is never typed); a pointer press anchors and a drag
  extends (the shell already forwards MOVE/UP to a pressed app window,
  both lanes -- no shell change); the ground is painted in the chosen
  blue with the text turned white on it, a nib past a wholly-selected
  line to show the break is taken.  Proven two ways at once:
  `test_textview` unit-checks the pure span logic (both anchor orders,
  multi-line replace, backspace-is-the-deletion, select-all);
  `ideselect.script` proves the three roads end to end -- shift+End
  with a ground pixel, a pointer drag, and Cmd-A -- each by reading the
  FILE back after a character lands on the span (the replaced word
  GONE, the character there).  Mutations by name: no collapse (every
  replace-assert fails, the plan's named one); no ground (the blue
  pixel goes white); no drag-extend (the dragged word survives).
  §5 decision: the behaviour lives in the KIT, so it does not fork --
  the simple editor inherits collapse-on-edit unchanged (its scripts
  still green) and never sets a selection, so it is safe; wiring the
  simple editor's own shift-select UI is a small follow-on with its own
  proof (shiftcaps is its harness), deferred because the IDE is the
  editor being built up here.
- **P4 — the clipboard, and an Edit menu.**  DONE 2026-08-28, and it
  went FURTHER than planned: the desk's clipboard is one store for the
  applications AND the shell's own fields.  What existed pointed the
  way -- `tiku_field` already kept a process-local text clipboard (a
  static inside the kit) -- so that store was promoted to
  `kits/interface/tiku_clipboard.{h,c}` (`set`/`text`/`stamp`, 4096
  bytes), the field rewired onto it, and every field in the shell was
  desk-wide at a stroke.  Services `clip_set`/`clip_get` appended;
  embedded they touch the store directly.  Out of process, ONE msg kind
  `TIKU_MSG_CLIP` ('clip', a "text" string) rides the EXISTING SAY/TELL
  roads -- no new wire verbs: a session SAYs its copy up, and the shell
  MIRRORS the store to every session whenever its stamp moves
  (`clip_mirror`, once a turn), so every session's copy is warm and a
  paste never waits on the wire; the client also writes its own cache on
  copy, so copy-then-paste inside one application never depends on the
  echo.  The store cannot say who changed it -- a field's Cmd-C deep in
  the kit, an embedded service, a session -- which is why it wears a
  STAMP and the shell watches it, rather than every changer learning to
  broadcast.  The editor: Cmd-X/C/V (+A) and an Edit menu (Cut, Copy,
  Paste, Select all) through one `edit_clip`; cut is copy plus the one
  backspace that removes a held selection; textview gained
  `selected()` (span extraction) and `insert_text()` (paste as typing,
  so it replaces a selection).  The panel's NAME field gained the same
  Cmd keys the rename field already had.  Proven in three scripts:
  `twowin.script` (EMBEDDED: pane Alpha puts, pane Beta gets, one
  store); `ideclip.script` (REMOTE: page to page across windows, page
  to the Save panel's field WHERE THE PASTE NAMES THE FILE BEING MADE,
  and field back to page replacing the selection still held there);
  the Edit menu's Paste proven as the same paste.  Mutations by name,
  each killing ONE leg: the shell not mirroring fails only
  field-to-editor; the shell deaf to a session's CLIP fails only
  editor-to-field; the embed clip_get dead fails only the twowin round.
  TRAP for the executing agent: the remote lane's client lives in
  `tiku-run`, which is NOT rebuilt by the app's `.so` target -- a stale
  runner takes the new services table and quietly lacks the entries.
- **P5 — undo and redo.**  DONE 2026-08-28.  `tiku_textview` keeps a
  journal (256 entries, oldest fall off) of each edit as an INSERT or
  a DELETE of text at a place; a `record()` folds a continuing
  keystroke into the run above (typed forward, or rubbed out just
  before/at it), so a word is one entry and one step to undo.  undo and
  redo REPLAY the inverse with recording off -- an insert is undone by
  deleting its length forward, a delete by re-inserting its text -- so
  there is no second copy of the edit logic, only its mirror.  The
  compound ops record ONE entry each: `sel_delete` keeps the span whole
  before the backspaces that take it apart, and `insert_text` (paste)
  filters to what a file may hold and records that, so a paste undoes
  whole and by exactly what it put in.  "Modified" stopped being a
  one-way flag and became the journal's DISTANCE from the mark a save
  leaves (`undo_at != saved_at`) -- so undoing back through a save
  reads as unmodified, and a save closes the run so the next keystroke
  is its own entry.  The editor: Cmd-Z / Cmd-Shift-Z and Undo/Redo at
  the head of the Edit menu, each enabled by `can_undo`/`can_redo`.
  Proven by `test_textview` (word folds to one undo, redo restores, a
  Return breaks the run, modified walks back to a save, paste is one
  entry) and `ideundo.script` end to end: type a word, save, undo,
  save -- the whole word gone (the file LACKS even the "xy" a
  letter-at-a-time undo would leave), then redo restores it.  Mutations
  by name: no coalescing (undo leaves "xy", the plan's named one); redo
  refusing (the word never comes back).
- **P6 — find.**  DONE 2026-08-28.  Cmd-F drops a `tiku_field` bar
  BETWEEN the toolbar and the page (the page's top became
  `edit_page_top()`, which every geometry site now asks -- rows, the
  caret, the selection ground, the scrollbar, the click arithmetic --
  so the page simply sits lower while the bar is open).  While it is
  open the bar has the keys; Return runs the find, Escape closes it.
  A hit becomes the SELECTION (`edit_select_span` -- anchor at the
  start, caret at the end), so P3 paints it and a character typed after
  Escape replaces it.  `find_forward` sweeps from the caret to the end
  and then, wrapping, from the top back to the caret, so the third
  Return after the last hit lands on the first.  For a BASIC file an
  ALL-DIGITS search is first a line NUMBER: the caret goes to the line
  that WEARS the number and its digits are selected -- and only if no
  line wears it is it looked for as text.  The bar's label says so
  ("Find / line:").  `idefind.script` proves all of it by reading the
  FILE after a character lands on each hit: the number jump replaces
  the LINE numbered 40, not the "40" sitting as text on line one (the
  caret is below both, so a text search would WRAP to the text one --
  which is exactly what the broken-jump mutation does); the word's
  third Return wraps to the first line, not the second where the caret
  sat.  Mutations by name: no wrap (the wrapped hit's replacement never
  lands, the plan's named one); no line-number jump (the text "40" is
  taken instead of the line).
- **P7 — renumber (BASIC only).**  DONE 2026-08-28.  The Edit menu's
  Renumber row (BASIC files only) gives every numbered line the next
  ten in file order and rewrites every branch to follow -- a number
  after GOTO / GOSUB / THEN / ELSE (the word matched case-insensitively
  at a boundary) is remapped through the same old->new table, while a
  number in a string or a REM is left alone.  It needed a journal
  primitive P5 did not have: `tiku_textview_group_begin/end` bracket a
  batch so undo and redo carry the WHOLE run of one batch together --
  so the select-all + insert_text that replaces the program is ONE
  Undo, which is the only guard the transform needs (no dialog).
  Building it surfaced a latent bug: `tiku_textview_selected` reserved
  a trailing newline the LAST line never has, so a span copied into an
  exactly-sized buffer lost its final byte -- which is why a grouped
  renumber first came back a character short.  Proven by `test_textview`
  (a batch undoes and redoes as one) and `iderenum.script` (5,7,23 with
  GOTO 23 -> 10,20,30 with GOTO 30; one Undo back to 5,7,23).  Mutations
  by name: branches not followed (the GOTO keeps 23, the plan's named
  one); undo not group-aware (a single Undo leaves the program gutted).
- **P8 — the long line.**  DONE 2026-08-28.  A per-editor `scroll_x`
  slides the whole page left so the measured caret stays inside the
  text area on a line longer than the window; Home takes the caret to
  column zero, whose x is zero, which pulls the slide back to nothing,
  so the view comes home on its own.  No wrap -- the line stays one
  line.  Every x that draws (the text, the caret, the selection ground,
  and the click-to-column arithmetic) now subtracts `scroll_x`; the
  scrollbar redraws over the gutter, so a long line needs no clip.
  `idehscroll.script` proves it by the caret: at a long line's end it
  stands near the RIGHT edge (000000 where, without the follow, the
  caret has run off-screen and the pixel is blank), and Home puts it
  back at the left with the right edge cleared.  Mutation: no rightward
  follow (the end-of-line caret never appears).
- **P8 — the long line.**  A horizontal follow: the painter takes an
  x offset that keeps the measured caret inside the page as it walks a
  line longer than the window; Home brings the view back.  No wrap
  (§4).  Proving: a 100-character line, caret pixel stays on the page.

Order matters: P1/P2 are catch-up the simple editor half-knows
already; P3 unlocks P4 and P6; P5 sits after P4 so Cut is undoable;
P7 rides P5's journal.  Phases are small enough for the house method
to hold whole.

**All eight phases are DONE (2026-08-27..28).**  The MVP page grew a
caret that goes everywhere and follows the pointer, a wheel, a
scrollbar and a status line, selection, a desk-wide clipboard and an
Edit menu, undo/redo, find with a number that is a line to go to,
renumber, and a horizontal follow for long lines.  What §4 deliberately
left out still stands as later arguments, not forgotten ones.

## 4. What is deliberately NOT in this plan

Word wrap, double-click word selection, bridging the clipboard to the
host X11 selections, replace and replace-all, completion, folding,
printing, per-editor font choice, split views, tabs, multiple carets.
Each is a later argument, not a forgotten one.

## 5. Two personalities, one behaviour

TikuC gets every phase free except P7 and the number-jump half of P6,
which gate on the language exactly as Run does today.  Anything built
into `tiku_textview` should be exercised once from the SIMPLE editor
too (arrows there already; selection/undo/clipboard should not fork
its behaviour from the IDE's) — one text engine, everywhere, is the
whole reason the kit owns it.

## 7. The second wave — audited 2026-08-28, three phases planned

Audited against the tree at `6205fbf`.  What the four asked-for
behaviours already have, and what is missing, verified by reading:

- **Syntax highlighting** -- keywords, strings and line numbers are
  DONE and were built right: the classifier (`tiku_syntax.c`) speaks
  the interpreter's own crunch table, and the ink enum
  (`tiku_gfx.h`) already tells apart WORD, STR, NUM, REMARK and FAINT
  -- the leading line number is painted as ink standing back, exactly
  the receded look asked for.  VFS paths as their OWN token class do
  not exist: a quoted path is inked as any string.  That is P9.
- **Auto line numbering** -- DONE, both halves.  A fresh page seeds
  `10 `, Return TYPES the next number by tens, a person never types a
  number; and RENUM exists as Edit > Renumber and the toolbar's own
  tool -- by tens, GOTO/GOSUB/THEN/ELSE following, one Undo.  Gaps are
  what Renumber is for, which is the listed design.
- **Case normalization** -- NOT built.  Nothing uppercases; the
  fixtures type lowercase and the interpreter accepts either.  P10.
- **Error squiggles** -- NOT built.  The ink table already carries
  TIKU_INK_WRONG, put there for exactly this and used by nothing.  P11.

- **P9 — the path is its own kind of thing.**  DONE 2026-08-28.
  `TIKU_INK_PATH` was APPENDED to the ink enum (so no existing value
  shifted) and derives its shade `tiku_dim(CODE_STR, SELECT)` -- the
  string's ink met halfway with the shade the desk gives a chosen
  thing, so it re-themes on every table and names no fourth colour.
  In `basic_spans` a quoted run whose first byte after the quote is `/`
  becomes THREE spans -- the opening quote STR, the body PATH, the
  closing quote STR (absent on an unterminated string, which runs to
  the line's end as strings already do).  Line-local, C untouched.
  `test_syntax` pins the split (`PRINT "/dev/led"` = wwwwwpsaaaaaaaas,
  a non-path string untouched, an unterminated path to the line end);
  `idepath.script` proves it in the running editor by GLYPH pixels --
  the path's 69435c a row apart from the plain string's a02020, the
  reserved word's blue above them both.  Mutation: the re-ink dropped
  -- the /dev/led glyph paints string red where the path ink was
  named, and that pixel fails.
- **P10 — keywords uppercase themselves, 1982-style.**  DONE
  2026-08-29, and SIMPLER than the plan feared: no new kit query was
  needed, because the CLASSIFIER is already the reserved-word authority
  AND already knows a keyword in a string from one in code.  App-side
  `edit_normalize_word` (IDE key road): when a non-word character or a
  Return ends a word, classify the line and find the run the finished
  word falls in -- if it is a WORD span, uppercase exactly those bytes.
  A keyword inside a string or a remark is a STR / REMARK span, so it
  is left alone for free, and a name that merely begins with a keyword
  (`PRINTER`) is a PLAIN span and untouched.  The rewrite is textview
  ops (place, backspace the word, insert upper) inside one
  `group_begin/end`, so the re-case is ONE undo unit -- the five
  backspaces coalesce and the insert is one entry, both in the batch.
  BASIC only.  `idecase.script` proves all three: `print`+space becomes
  `10 PRINT`; `print "goto`+space keeps goto lower while PRINT shouts;
  and after `let`+space, two undos (the space, then the batch) give
  back `30 let` -- which only holds because the re-case is grouped.
  Mutations: the normalizer dropped (nothing shouts, `10 PRINT` fails);
  the group dropped (two undos overshoot to empty, `30 let` fails while
  the uppercase rounds still pass -- so the grouping is what is pinned).
  A script types a quote with `key "` (single-char keys pass through).
- **P11 — error squiggles, one grammar two hosts.**  The grammar that
  must judge a line is the interpreter's, or the editor teaches lies
  -- the same rule the highlighter was built under.  The interpreter
  already builds and runs on the host (`basichost/`, the same
  `tiku-basic` the Run button spawns), so the road is: give the HOST
  BINARY a `-check <file>` mode that LOADS the program and reports,
  without running it, every line the cruncher and the statement
  dispatch can refuse -- `line N: what` on the pipe, one per fault,
  exit says whether any.  Honesty the plan must keep: a 1982
  interpreter defers much to runtime, so -check catches what can be
  known STATICALLY (a word no dispatcher owns, an unclosed quote, a
  malformed expression where the parse helpers can be run dry) and no
  more; the bound grows as the kernel's own checking grows, and the
  editor inherits every improvement free, because it is ONE grammar.
  Editor side: on SAVE (and on open), run `-check` through the same
  bounded pipe machinery Run uses; mark the lines it names; paint a
  wavy underline under each marked line's text in `tiku_ink(WRONG)`
  -- the ink that has been waiting for this.  Marks clear on the next
  save.  Never per keystroke: a child per keypress is a fork bomb
  wearing a nicety's name.  Proving: save a program with an unclosed
  quote -- a WRONG-ink pixel under that line; fix it, save, the pixel
  is the page again; and the check road proves itself end to end
  because the same binary run by hand says the same line.  Mutations:
  the editor never spawning -check (no squiggle, named pixel fails);
  -check reporting nothing (same fail, from the other host).

Order: P9 is an afternoon; P10 rides the kit query and the journal
grouping that already exist; P11 is the deep one and starts in the
kernel's basic, which is also what makes it worth doing.
