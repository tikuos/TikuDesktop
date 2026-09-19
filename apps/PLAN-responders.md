# Responders: one interface for every surface on the desk

*2026-09-19.  A plan, not yet built.*

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

### R0: the interface and the damage union

- Add `tiku_trk_responder.h` in `TikuTracker/src/ui/` with the two
  structs above and a `tiku_trk_responders_t` list with `push`,
  `remove`, `deliver` and `paint`.
- The desktop loop keeps a list, empty at first, and unions what
  `deliver` returns into `desktop_damage`.  Nothing is on the list yet,
  so nothing changes on screen.
- Proof: `test_frames` and the frame report unchanged; a new
  `test_responders` drives two fake responders through hit order,
  capture and the returned damage.

### R1: the Deskbar

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

### R2: popups and overlays

- Popup menus, submenus, the spring-loaded navigator, the status
  overlay, the force-quit overlay and the Info window's hover pop-up
  each become a responder with a frame of its own.  The Info pop-up
  stops painting inside another window's draw, which is what defeated
  bounded frames for it this week.
- Proof: hoverpop, menuhovertrk, springnav, forcequit and the alert
  lanes; the Info hover site in `desktop_pointer_move` returns to a
  bounded rectangle.

### R3: folder windows and the workspace

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

### R4: the services table

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

### R5: rows carry their own commands

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
