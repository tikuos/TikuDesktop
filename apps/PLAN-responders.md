# Responders: one interface for every surface on the desk

*2026-09-19.  R0 to R4 built that day, R5 to R7 the next.*

## 1. What is wrong today

Three shapes in the desktop shell make every change cost more than it
should.  The numbers are from the tree at commit 8db830b.

1. **One struct and one event function.**  `desktop_t` in
   `tiku_trk_desktop_private.h` has about 520 fields.  The pointer-down
   handler in `tiku_trk_desktop_events.c` is about 1,100 lines of nested
   `if` chains that each end in `return`, and the file is 3,098 lines.
   Every gesture added this month (a held icon, a deferred launch, a
   hover that repaints only its row) went in as new fields on the struct
   and new branches in the chain.  Nothing owns a gesture, and the order
   of the branches is the dispatch policy, which no test states.
2. **Repaint is a global flag.**  `app->dirty = 1` is written from 432
   places.  Bounded frames were possible only because the few hot sites
   could be enumerated by hand; every other site still asks for the
   whole screen.  A frame's cost has no owner.
3. **Wiring is by hand-copied pointers.**  `tiku_trk_folder_render_t`
   has 44 fields, most of them pointers into the desktop's own state,
   and `tiku_trk_drop_controller_init` takes 20 arguments.  A new fact
   a view needs means a new field, a new argument, and a matching edit
   in `tiku_trk_desktop_lifecycle.c`.

Smaller things follow from these.  The application registry is
matched by string compare in 12 places.  `open_one` names three MIME
types before it consults the handler table.  The Deskbar's row kinds
number 24 and are switched on at 36 sites.  Seven `extern` globals
carry process state.  The script harness synthesised a Deskbar pick as
a press without a release until this week.  Lanes probe 230 pixels at
fixed coordinates, and adding an icon broke one.

## 2. The shape of the answer

**A surface is a responder.**  Each thing that can be pointed at or
drawn (the Deskbar, the workspace's windows, a folder window's content,
an Info window, a panel, a popup menu, a floating overlay) implements
the same small interface against its own state:

```c
typedef struct tiku_trk_responder {
    int  (*hit)(void *self, int x, int y);              /* is it mine */
    tiku_rect_t (*press)(void *self, const tiku_event_t *ev);
    tiku_rect_t (*move)(void *self, const tiku_event_t *ev);
    tiku_rect_t (*release)(void *self, const tiku_event_t *ev);
    tiku_rect_t (*key)(void *self, const tiku_event_t *ev);
    void (*paint)(void *self, tiku_surface_t *s);
    tiku_rect_t (*frame)(void *self);
    void *self;
} tiku_trk_responder_t;
```

Every handler returns the rectangle it changed, or a whole-screen
rectangle, or an empty one.  The loop keeps a z-ordered list, asks
`hit` from the top down, delivers to the first taker, and unions what
comes back into the frame's damage.  `app->dirty` goes away.

**A capture is explicit.**  A press that takes a responder keeps moves
and the release routed to it until the release, which is what the
`press_*` fields, `deskbar_press`, `menu_launch`, `sbar_fl`,
`prefs_track`, `embed_press` and `remote_press` do today by hand.  One
`capture` pointer on the loop replaces them.

**Views ask the shell for facts through one services table**, the way
applications already ask the desktop through `tiku_app_services_t`:

```c
typedef struct tiku_trk_shell_services {
    const tiku_trk_app_t *(*app_of)(void *ctx, const char *path);
    const char *(*art_for)(void *ctx, const tiku_model_t *m);
    int  (*open)(void *ctx, const char *path);
    int  (*launch)(void *ctx, const tiku_trk_app_t *a,
                   const char *const *docs, int n);
    void (*status)(void *ctx, const char *text);
    void (*damage)(void *ctx, tiku_rect_t r);
    void *ctx;
} tiku_trk_shell_services_t;
```

## 3. Rules that hold throughout

- A milestone moves one surface and leaves the rest on the old chain.
  Both paths run at once until the last surface moves; the old chain
  is deleted in one commit at the end, not thinned as it goes.
- Every milestone is proved by lanes that already exist.  A milestone
  that needs a new lane to pass has changed behaviour, which is a
  separate commit with its own message.
- No responder reaches into `desktop_t`.  It gets the services table
  and its own state, nothing else.  `check_architecture.py` gains a
  rule for it once the first responder lands.
- A handler returns what it changed.  A handler that cannot say
  returns the whole screen, and that is a bug to fix, not a default to
  keep.

## 4. The milestones, in the order they pay back

### R0: the interface and the damage union  (DONE 2026-09-19)

- Add `tiku_trk_responder.h` in `TikuTracker/src/ui/` with the two
  structs above and a `tiku_trk_responders_t` list with `push`,
  `remove`, `deliver` and `paint`.
- The desktop loop keeps a list, empty at first, and unions what
  `deliver` returns into `desktop_damage`.  Nothing is on the list yet,
  so nothing changes on screen.
- Proof: `test_frames` and the frame report unchanged; a new
  `test_responders` drives two fake responders through hit order,
  capture and the returned damage.

### R1: the Deskbar  (DONE 2026-09-19)

- `tiku_trk_deskbar_click`, `_motion`, `_release`, `_pointer` and the
  menu tick move behind a responder in `tiku_trk_desktop_deskbar.c`.
  The pick dispatch stays where it is; the responder calls it.
  `deskbar_press`, `menu_launch` and the dock-drag fields become the
  responder's own state.  The hover area the bar already reports
  becomes its `move` return value.
- The Deskbar branches leave `desktop_pointer_down`, `_move`, `_up`.
- Proof: dbmenu, deskbarhover, menuhover, clocktip, darkswitch,
  shutdownask, applink, applaunch, deskbarfont, and the hover
  assertion in `test_frames`.

### R2: popups and overlays  (DONE 2026-09-19)

- Popup menus, submenus, the spring-loaded navigator, the status
  overlay, the force-quit overlay and the Info window's hover pop-up
  each become a responder with a frame of its own.  The Info pop-up
  stops painting inside another window's draw, which is what defeated
  bounded frames for it this week.
- Proof: hoverpop, menuhovertrk, springnav, forcequit and the alert
  lanes; the Info hover site in `desktop_pointer_move` returns to a
  bounded rectangle.

### R3: folder windows and the workspace  (DONE 2026-09-19)

- A folder window's content (rows, band, rename, scrollbar, title
  strip, drag arming) becomes one responder per window.  The workspace
  (tabs, moves, resizes, the rise and the ghost) becomes one.  The
  press context (`press_at`, `press_x/y`, `press_row`, `press_fl`,
  `press_window`, `press_focus`, `press_down`, `press_menu`) becomes a
  struct owned by the loop's capture.
- `desktop_pointer_down` is under 200 lines when this lands, or the
  milestone is not done.
- Proof: the drag and drop lanes (dragghost, dupdrag, appdrop,
  applink, dropmenu), iconpress, the band and rename lanes, and
  `test_render_perf`'s drag parity.

### R4: the services table  (DONE 2026-09-19)

- `tiku_trk_shell_services_t` replaces the pointer fields in
  `tiku_trk_folder_render_t` and the first fifteen arguments of
  `tiku_trk_drop_controller_init`.  The render struct keeps geometry
  and the damage list only.
- `desktop_app_at` becomes the one registry lookup; the 12 string
  compares call it or go.  "Is an application" becomes a fact set on
  the model at stat time (`facts.application`), so a view never asks
  per frame.
- The seven `extern` globals move onto `desktop_t` or the options
  struct.
- Proof: `check_architecture.py` and `check_kits.py`; the appdrop,
  applink, openwithwin and documentassociation lanes; a count of
  `render_t` fields under 20.

### R5: rows carry their own commands  (DONE 2026-09-20)

- A Deskbar row carries a command id and an icon name.  The 24 item
  kinds shrink to the structural ones (separator, expander, disabled,
  row).  The builder in `tiku_trk_deskbar_leaf.c` sets the command;
  the clicker and the painter stop switching on kind.
- `open_one` stops naming MIME types.  The type database gains a
  "prefer the editor" flag for `text/x-basic`,
  `application/x-tiku-project` and `text/plain`, and the handler table
  answers.
- Proof: `test_deskbar_dock`, `test_app_groups`, dbmenu, apps,
  appgroups, documentassociation, ideopen.

### R6: the harness drives responders

- The script adapter delivers one gesture vocabulary (press, move,
  release, key) to the same responder list the host drives, so a
  `menu-pick` and a `click` are the same thing by construction.
- Lanes that probe chrome by pixel get a named probe instead:
  `expect-row-ground <menu> <row>`, `expect-icon-of <name> <art>`.
  Probes that are about pixels (a rendered icon, a colour setting)
  stay as they are.
- Proof: the lane count is unchanged and the whole suite holds; the
  number of `expect-pixel` lines in lanes that test menus or the
  Deskbar drops to zero.

### R7: delete the old chain

- The `if` chains in `desktop_pointer_down`, `_move`, `_up` and
  `_key_down` are gone; the loop calls `deliver`.  `app->dirty` is
  gone; `desktop_damage` is the one way to ask for a frame.
- Proof: `grep -c "dirty = 1"` is zero; the full suite holds; the
  frame report shows partial frames for every hover and drag lane.

## 5. What is not changing

- The event loop stays one thread that owns all state.  Responders are
  a dispatch shape, not a threading one; the worker rules in
  `PLAN-loop.md` stand.
- `tiku_app_services_t` and the application ABI do not change.  An
  application never sees a responder.
- The kits stay free of desktop types.  The responder header lives in
  `TikuTracker/src/ui/`, not in `kits/`.
- Lanes keep their verbs.  New verbs are added; none is removed until
  no lane uses it.

## 6. Traps, named before they are met

- **Two paths at once.**  Until R7 a gesture can be taken by the new
  list and then by the old chain.  The loop asks the list first and
  skips the chain when a responder took the event; a milestone that
  forgets this shows up as a double action (a launch twice, a rename
  armed twice).  `test_responders` checks the skip.
- **Capture across a surface that closes.**  A popup that closes while
  it holds the capture leaves the release with nobody.  `remove` clears
  the capture when it removes the holder.
- **Damage that lies.**  A handler that changed more than it returned
  leaves a stale patch.  The frame report's `partial` count going up
  while a lane's `expect-same-as-redraw` fails is the signature.
- **The harness reading the old state.**  Script verbs that inspect
  `desktop_t` fields directly (`pose_at`, `tab_point`, the selected
  name) must be repointed at the responders' state as each moves, or
  lanes pass against a field nothing updates any more.
- **Load-sensitive lanes.**  The suite must run on an idle machine; a
  dozen lanes fail spuriously under load and will look like a
  milestone's regression.  Compare against a worktree at the previous
  commit before believing one.

## 7. What R0 settled

- A handler returns two things, not one: whether it took the event and
  what it changed.  The plan's sketch returned the damage alone, which
  cannot say "not mine"; with both paths live, "not mine" is what lets
  the old chain run.  The signature is
  `int (*press)(void *self, const tiku_event_t *ev, tiku_rect_t *damage)`.
- A damage is a rectangle; "everything" is a rectangle with a negative
  width, so it cannot be returned by accident, and the union swallows
  anything into it.  `desktop_take_damage` turns it into the frame
  request the loop already understands.
- A tick is part of the interface.  The bar's long press and its
  submenu dwell are timed, and a surface with no tick would need the
  loop to know its clocks.
- A press asks only the surfaces whose `hit` says yes, top down; a move
  and a release ask the capture holder first and then everyone, since a
  hover and an auto-hide edge belong to surfaces the pointer is not
  over.  A key asks everyone top down.

## 8. What R1 settled

- The Deskbar keeps its own state in `desktop_bar_t`: the double-click
  memory, the armed CMD press, the resize of the bar, the held primary
  button and the program's row waiting for its release.  Nine fields
  left `desktop_t`, and `desktop_press_hold` no longer knows the bar.
- The responder is asked at the point in the old chain where the bar's
  block stood, after a popup menu and the global bar have had their
  say.  Two presses that the old chain answered before anything else, the
  resize strip and the CMD press, now come after a popup: with a popup
  open, either closes the popup instead of starting on the bar, which
  is the better answer and no lane depended on the other.
- The bar's move is never "taken", so the rest of the desk hears every
  move, as it did; the bar's auto-hide now also answers during a
  scrollbar drag, which the old chain returned from before reaching it.
- The lanes never had to change: 21 Deskbar lanes and the whole suite
  hold, and `test_frames` attaches the bar to a bare desktop's list the
  way the lifecycle does.

## 9. What R2 settled

- The list is consulted at the top of every handler now, since the
  surfaces that came before the Deskbar in the old chain are all on it:
  the force-quit picker, the alert and progress overlay, and the popup
  menus with the navigator that rides them.  What still follows the old
  path (tracking windows, the Info window's own clicks, the global bar)
  comes after, which changes one thing: a press on the Deskbar while one
  of its menus is out is the Deskbar's before it is the global bar's.
- A key handler is asked on the up edge as well, because the popup menu
  relabels its rows as Shift goes down and comes up.
- The Info window's field pop-up has a frame of its own: its hover
  repaints the box it left and the box it stands in, not the screen.
  It paints from the list at the slot the desktop drew it in; the popup
  menus still paint from the desktop, since they must come above the
  window bars, which move in R3.
- A scripted menu pick was a press without a release, which left the
  popup holding the capture and swallowing the next press.  Two fixes:
  the harness owes a release after any pick, as after a click, and a
  holder that no longer claims the point of a new press has lost its
  release, so the capture ends there.
- The spring navigator's tracking is a responder; its drop on release
  is part of the drop path and waits for R3.

## 10. What R3 settled

- Four more surfaces: the workspace's frames with the windows that
  answer for themselves (preferences, panels, embedded and remote
  applications, the Find window), the folder windows' content with the
  desk's own, the Info windows' parts, and the global bar's strip.  The
  pointer handlers are the delivery alone: about 1,300 lines left
  `tiku_trk_desktop_events.c` for three files of their own.
- The order on the list is the order of the old chain, read off it:
  the folders under the frames, the frames under the Deskbar, the bar
  under the global strip, the strip under the Info windows' parts, and
  the overlays over all.  A press on the strip taken by the desk's own
  folder, before the strip was on the list, was the one regression the
  lanes found.
- The desktop's click model moved off the loop struct onto the desktop,
  since the folders responder has no loop to reach; the Tracker shell
  keeps its own on its loop.
- The press context (`press_at`, `press_row`, `press_fl` and the rest)
  stays as fields for now: the folder renderer and the Tracker shell
  read them by name, and the struct move is its own commit.
- Keys are not moved.  The folder windows' keyboard (type-ahead, the
  arrows, a rename in progress) is still `desktop_key_down`'s, about
  680 lines, and waits for its own milestone.

## 11. What R4 settled

- `tiku_trk_services_t` carries four answers: the application behind a
  path or a link, the art its pose wears, a line for the status strip,
  and a frame for what changed.  The folder renderer and the drop
  controller ask through it; the desktop fills it once.
- The render struct's copied pointers went by their owners: one pointer
  each to the inline editor, the band, the drop target and the press,
  where there were seventeen to their fields.  The struct has 30 fields
  now, from 44; the twenty that remain are geometry, damage and the
  kit objects a row is drawn with, and going under 20 means splitting
  the geometry out, which is not this milestone's.
- The press became `tiku_trk_press_t`, declared with the renderer since
  it reads it, and one struct on the desktop.  The Tracker shell keeps
  its own press fields; they were never the renderer's to read.
- The drop controller's registry matching is gone: it asks the services,
  and `desktop_app_at` matches what it matched (a module, a command, a
  path, through a link).  Of the twelve string compares, four remain in
  that one lookup, three deduplicate registrations, and one in the
  startup panel wants the art service and waits.
- The drop controller's init lost three arguments (a status buffer, its
  size, the dirty flag) for the table; the rest stay, since each is a
  part it acts through rather than a fact it asks for.
- The seven externs were declarations of nothing: names of locals in
  the events file, in a generated block.  They are gone.
- "Is an application" is not a fact on the model: the backend that
  stats a file knows no registry, and a fact set after the stat is the
  service call by another name.

## 12. What R5 settled

- A Deskbar row is one of four things: a row that acts by its command,
  a separator, a note that cannot be chosen, or an expander that opens
  the submenu its `expand` names (a folder, a recent list, a group of
  programs, the settings).  Twenty-four kinds became four and a builder
  enum of four; the clicker copies the command into the pick and the
  painter paints the icon the builder named.  The one row that acts
  inside the bar, showing or hiding replicants, has a command of its own
  that the clicker answers before making a pick.
- Two kinds, About and Help, were never built into the leaf menu; they
  went with the rest.  The recent-programs rows wear the application
  art now, where the old painter drew a file for them.
- A document opens in its handler even when it could be run because the
  type database says so: a record carries the word `document` among
  its columns, and the shipped defaults say it for plain text, BASIC
  source and a Tiku project.  `open_one` names no type.  A types file
  written before this flag has none of the three rows, and a user who
  kept one adds them by hand; the defaults are installed only where no
  file is.

## 13. What R6 settled

- The harness already drove the responders: a `click`, a `menu-pick`
  and a `key` are events injected into the same queue the display's
  events come through, and the desktop delivers them to the list.
  Nothing changed there.
- A lane about chrome reads a noun instead of a pixel.  The shell says
  `hot` (the label of the row under the pointer in whatever menu is
  open, the popup's deepest level first, then any of the Deskbar's
  menus), `tip` (the text floating beside the pointer: an Info field's
  pop-up or the Deskbar's tooltip), `strip` (whether the global bar is
  up), `minimized` (the minimized windows by title) and `menu-bottom`
  (where the leaf menu ends).  The browser shell says `hot` too.
- Two verbs joined `expect-read`: `expect-said <noun> [text]` compares
  the whole answer, so "no row is hot" and "nothing floats" are checks
  and not a pixel of the backdrop; `expect-over <noun> <n>` is the
  mirror of `expect-under`.
- Eight lanes read nouns now (deskbarhover, menuhover, menuhovertrk,
  clocktip, hoverpop, globalbar, dbwinrow, deskbarfont); the lane
  count is 264 as before.  The pixel probes that stay in menu lanes are
  about drawn art: the expander arrow in dbmenu, the row icons in
  dbmenuicons and menuicons.  The two probe names the plan proposed,
  `expect-row-ground` and `expect-icon-of`, were not built: the first
  is `expect-said hot`, the second is a pixel by nature.
- Proof: `test_deskbar_hover` names the hot row through the leaf and
  its submenu and hears nothing once the pointer left; `test_frames`
  asks the desktop's nouns; both fail with the leaf branch stubbed.

## 14. What R7 settled

- The key chain is out of the events file, which is 378 lines now and
  holds the loop's turn and four entry points that each call one
  `deliver`.  The chain became four `key` handlers in the order it ran:
  the Info windows' keys on the Info responder; the focused window's
  text owners (a panel's field, the search, an application's or a remote
  window, the inline editor) and the Cmd-Q family on the windows
  responder; the desk's own chords (F1, the add-ons, the panels, the
  selection and clipboard chords, undo, the standard set, the view
  modes, the switcher, closing a window, the settings) on a keys
  responder of their own, pushed between the folders and the windows,
  with F1 on a help responder above the Info windows;
  the folder's navigation on the folders responder.  Each handler starts
  on the down edge, as the chain did.
- F1 kept its priority: it ran before every other key of the chain, and
  an application window that takes every key would have kept the reader
  away, so it has a responder of its own pushed above the Info windows.
- `desktop_damage_all()` is the one way a full frame is asked for in the
  shell: the 171 `dirty = 1` writes in the desktop's own files are calls
  to it, and the field is written by the painter alone.  Not done: the
  sixteen UI modules under `src/ui` still take an `int *dirty` handle
  from `lifecycle.c` and write it 68 times, and the loop reads the field
  to decide whether to paint.  Turning the handle into a bounded request
  is a per-module design, one module at a time, not a sweep.
- Proof: every unit test passes, six of which stub the request because
  they link one desktop file without the painter; the lanes that drive
  each moved key group pass; the full suite holds.
