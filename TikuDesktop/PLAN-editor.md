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
- **P5 — undo and redo.**  Kit: a journal in `tiku_textview` — entry
  (kind, line, col, text), runs of plain typing COALESCED into one,
  bounded (256 entries, oldest fall off); undo/redo walk it; the
  modified flag follows the journal's distance from the saved mark, so
  undoing back to the save clears `(changed)`.  Edit menu gains
  Undo/Redo.  Proving: type a word, undo ONCE removes the word (the
  coalescing is the point), redo restores; undo to the mark then the
  strip says unchanged (assert by saving and comparing the file to the
  original).  Mutation: break coalescing — undo removes one character
  and the named assert fails.
- **P6 — find.**  Ctrl-F drops a find field under the toolbar
  (`tiku_field`); Return finds forward from the caret, wraps, reveals,
  and the hit becomes the SELECTION (P3 paints it, typing replaces
  it); Escape puts the field away.  A search that is all digits and
  matches a BASIC line number jumps to that LINE — goto-line for a
  language whose lines wear their own numbers, no second dialog.
  Proving: two hits of one word prove wrap; find-then-type proves the
  selection; a digits search lands the caret (pixel).  Mutation:
  break the wrap.
- **P7 — renumber (BASIC only).**  A `Renumber` row: rewrite the
  numbers by tens in file order AND rewrite every GOTO/GOSUB/THEN/ELSE
  target to follow.  No confirmation dialog — it is ONE journal entry
  and P5's Undo is the answer.  Proving: plant `5,7,23` with `GOTO
  23`; renumber; the file says `10,20,30` and `GOTO 30`; undo restores
  the file.  Mutation: leave GOTO targets unrewritten.
- **P8 — the long line.**  A horizontal follow: the painter takes an
  x offset that keeps the measured caret inside the page as it walks a
  line longer than the window; Home brings the view back.  No wrap
  (§4).  Proving: a 100-character line, caret pixel stays on the page.

Order matters: P1/P2 are catch-up the simple editor half-knows
already; P3 unlocks P4 and P6; P5 sits after P4 so Cut is undoable;
P7 rides P5's journal.  Phases are small enough for the house method
to hold whole.

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
