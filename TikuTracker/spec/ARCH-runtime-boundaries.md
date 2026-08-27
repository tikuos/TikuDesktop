# Desktop runtime and embedded application boundaries

## Decision

The desktop runtime hosts applications. Tracker is one application; it is
not the owner of the window system, platform event loop, or other apps.

```text
X11 / canvas adapter
        |
desktop runtime (events, windows, focus, timers, app registry)
        |
embedded application API
        |
        +-- Tracker app -- Tracker UI -- Tracker core -- backend
        +-- another app
```

The first application registry is statically linked. Dynamic loading is a
separate decision after lifecycle and service ownership are proven by a
second application.

Tracker is selected from that registry as `org.tikuos.tracker`; the browser
shell uses `org.tikuos.tracker.browser`. Their existing blocking loops enter
through the runtime's compatibility callback. New embedded applications use
the nonblocking start, event, tick, and stop lifecycle.

## Dependency rules

1. No kit in `applications/kits` includes a `tiku_trk_*` header; a kit
   that knows the application above it has stopped being a kit.
2. Tracker core never includes X11 or the Interface kit's drawing headers,
   `tiku_gfx.h`, `tiku_font.h`, and `tiku_ui.h`; the kit's `tiku_event.h`
   is shared input vocabulary rather than a surface, so core may take it.
3. Platform headers and native event types do not cross the runtime adapter.
4. Shared input, view-mode, and column vocabulary lives in small definition
   headers rather than in the PoseView API.
5. A window owns its controller state and receives its own draw, event,
   close-request, and destroy callbacks.
6. The runtime owns z-order, focus, input routing, invalidation, and window
   lifecycle. Tracker owns file-management behavior.
7. Cross-layer access goes through an explicit services table. An app does
   not reach into the runtime's private state.

## Library shape

The kits publish that split, one archive per kit -- six in all, of which
three carry this document's boundaries:

- `kits/interface` -> `libtiku_interface.a`: the surface, fonts, the
  colour table, the R5 control set, windows and focus, the platform
  adapter behind the portable event boundary -- and the interaction
  state behind two of the controls, the scrollbar's tracking
  (`tiku_scroll`) and the text field's editing (`tiku_field`), which
  draw nothing and are checked for it, so an application gets the
  behaviour and not just the picture;
- `kits/application` -> `libtiku_application.a`: embedded application
  descriptors, the registry they are selected from, their lifecycle, and
  the client, conductor, and remote channels;
- `kits/translation` -> `libtiku_translation.a`: icons and images, one
  kit further out because a picture becomes a bitmap before the interface
  kit ever draws it.

Tracker publishes no libraries: it compiles its own sources directly into
`build/TikuTracker` and `build/TikuDesktop` and links the six kit archives.
Model, filesystem work, query and state now belong to the Storage kit.

The source tree mirrors those boundaries: `core/` contains portable models,
`ui/` owns Tracker views and controllers, `ui/windows/` owns feature-window
lifetimes, `ui/input/` owns retained pointer gestures, and `ui/overlays/`
owns modal and operation-status presentation. `deskbar/` owns the bar and
Desktop-icon behavior, and `shell/` contains front-end composition and
application entry descriptors. Its `adapters/` and `session/` children
isolate script observations and persisted lifecycle. Namespace and
file-operation adapters now live in `applications/kits/storage`, and icon
and image handling in `applications/kits/translation`; Tracker links
against those kits rather than carrying adapter or media directories of
its own.

## Extraction order

1. Move shared vocabulary out of implementation headers and enforce the
   dependency rules in the build.
2. Add a platform-neutral event and host interface to the interface kit
   at `applications/kits/interface`.
3. Move the generic window manager out of the Tracker namespace and give
   windows event/lifecycle callbacks.
4. Register Tracker through an embedded application descriptor.
5. Extract the multi-window shell by vertical feature: folder window,
   Desktop view, Find, file panel, Info, preferences, selection, commands,
   session, and drag controller.
6. Reuse the same folder controller in the spatial and single-window shells.
7. Prove the boundary with a small non-Tracker app before designing a plugin
   ABI or process protocol.

The vertical extraction has started with folder rendering, the Desktop view,
Find, Get Info, the Open/Save panel, preferences, Select by pattern, and the
application switcher. Their headless state machines remain in
`tiku_trk_poseview`, `tiku_trk_find`, `tiku_trk_info`, `tiku_trk_panel`,
`tiku_trk_prefs`, `tiku_trk_select`, and `tiku_trk_switcher`; the paired
controllers now own rendering, input, persistence or refresh, and the
lifetime of their composited windows. The Desktop controller additionally
owns its union source, placement, watch, poll, and backdrop lifetime, while
the Deskbar roster bridge owns host-process discovery and workspace window
publication. The shell supplies folder render services, but no longer paints
or hit-tests folder content. Folder menu context, per-type attributes,
Open-with candidates, launch presentation, and direct view commands are
likewise owned by controllers under `ui/commands`. Filesystem mutations,
clipboard actions, drop execution, and secondary-drop decisions receive
their dependencies through explicit service records. Folder open/raise,
frame and query-result lifetimes, inline value edits, and entry renames are
owned under `ui/windows`; rubber-band selection, drag target tracking,
auto-scroll, and spring-open timing are owned under `ui/input`. Session
restore/save and script presentation observations no longer reach through
the desktop composition function.
The status overlay owns alert and progress rendering plus modal input, while
filesystem answers remain a shell callback.

## Focus and disabled: the rules, and why there is no focus manager

Two rules, both enforced by the suite rather than by machinery:

1. **A focus ring or caret is drawn only while its window owns the
   keyboard** -- the same `tiku_workspace_focused()` test the window's
   own key handler opens with.  There is deliberately NO generic focus
   manager or tab order in the kit: the windows' focus is SEMANTIC
   (which field of the Find panel, which row of a rule), not a uniform
   list of controls, and Tab already means four different things across
   the shells.  A widget that wants a ring takes `TIKU_S_FOCUS` from its
   window's own gate.

2. **Disabled ink is `tiku_dim(ink, ground)`** -- the ink pulled halfway
   toward whatever it sits on.  Never `tiku_tint` toward white or black:
   either direction inverts on one of the theme tables, which is how a
   greyed menu command came to be BRIGHTER than a pickable one under
   dusk.  And a control that is drawn disabled must also be inert on
   every dispatch path -- the kit's menu refuses the pick; a shell
   branch that forgets is the bug class test_menu now pins.

## Two front-ends, one tree

The directories above are the real structure, and binary ownership
deliberately is not: the two shells are one product built two ways, and
the tree is organised by LAYER because that is where the boundaries are.
Measured from the link lists (2026-08-25), of the sixty-seven sources
under `src/`:

- thirty-three are linked into BOTH binaries -- the models, the pose
  engine, the menus, the deskbar's icon layer: the product itself;
- twenty-nine are the desktop's alone: its composition and splash, the
  Deskbar proper, the session controller, and nearly every feature
  window and command controller -- the single-window shell answers
  those with its own inline forms instead;
- exactly TWO are TikuTracker's alone: `shell/tiku_trk_app.c`, its
  composition, and `ui/tiku_trk_navigator.c`, its Back/Forward/Up --
  navigation the spatial shell deliberately has no counterpart for;
- the script engine (`shell/tiku_trk_script.c`, its adapter, and the
  stub) belongs to the build FLAVOURS, not to either front-end: the
  engine links into `build/dev/*`, the refusing stub into the product.

That remainder is a checked fact, not prose: `make print-ownership`
answers from the link lists themselves, and
`tools/check_architecture.py` compares the answer against the declared
`TRACKER_ONLY` set on every `make test`.  A file that quietly leaves one
binary's link line turns the suite red the same run.

A reader looking for "the desktop's source" should also know the
delivery arrangement: the desktop shell is COMPILED here, from this
tree, and `applications/TikuDesktop` -- the app framework beside this
repository, whose own documents are `R5-SPEC.md` and
`PLAN-hybrid-apps.md` -- fetches the built binary into its `build/` with
`make shell`, so it is also found under the name it wears.  The agent
surface this tree exposes is specified in `AGENT-SURFACE.md` beside this
file.

## Temporary exceptions

Both shells and the script driver consume portable events through the desktop
host. The X11 migration allowlist in `tools/check_architecture.py` is empty;
new native imports fail the architecture check. Composited windows, z-order,
focus, tracking, chrome, and application-owned lifecycle callbacks now live in
the desktop runtime's `tiku_window` interface.

Refactor commits do not change `FEATURES.md` verdicts unless they change a
user-visible behavior. Every extraction must pass the complete unit and
headless shell suite.
