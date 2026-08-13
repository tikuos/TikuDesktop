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

1. `applications/desktop` never includes a `tiku_trk_*` header.
2. Tracker core never includes X11 or `tiku_desk_*` drawing headers.
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

`applications/desktop` will publish:

- `libtiku_desk_ui`: surface, font, colors, icons, and controls;
- `libtiku_desk_runtime`: events, windows, focus, timers, app registry, and
  platform adapters.

Tracker will publish:

- `libtiku_tracker_core`: model, backends, filesystem work, query, state,
  type, launch, and task rules;
- `libtiku_tracker_ui`: pose engine and Tracker-specific controllers;
- `tracker_app`: lifecycle and composition only.

The source tree mirrors those boundaries: `core/` contains portable models,
`backends/` owns namespace and file-operation adapters, `ui/` owns Tracker
views and controllers, `media/` owns icon and image handling, `deskbar/` owns
the bar and Desktop-icon behavior, and `shell/` contains only front-end
composition and application entry descriptors.

## Extraction order

1. Move shared vocabulary out of implementation headers and enforce the
   dependency rules in the build.
2. Add a platform-neutral event and host interface to `applications/desktop`.
3. Move the generic window manager out of the Tracker namespace and give
   windows event/lifecycle callbacks.
4. Register Tracker through an embedded application descriptor.
5. Extract the multi-window shell by vertical feature: folder window,
   Desktop view, Find, file panel, Info, preferences, selection, commands,
   session, and drag controller.
6. Reuse the same folder controller in the spatial and single-window shells.
7. Prove the boundary with a small non-Tracker app before designing a plugin
   ABI or process protocol.

The vertical extraction has started with Get Info, the Open/Save panel,
preferences, Select by pattern, and the application switcher. Their headless
state machines remain in `tiku_trk_info`, `tiku_trk_panel`, `tiku_trk_prefs`,
`tiku_trk_select`, and `tiku_trk_switcher`; the paired `*_window` controllers
now own rendering, input, persistence or refresh, and the lifetime of their
composited windows. The desktop shell only opens them and reacts to semantic
results.

## Temporary exceptions

Both shells and the script driver consume portable events through the desktop
host. The X11 migration allowlist in `tools/check_architecture.py` is empty;
new native imports fail the architecture check. Composited windows, z-order,
focus, tracking, chrome, and application-owned lifecycle callbacks now live in
the desktop runtime's `tiku_desk_window` interface.

Refactor commits do not change `FEATURES.md` verdicts unless they change a
user-visible behavior. Every extraction must pass the complete unit and
headless shell suite.
