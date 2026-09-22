# Desktop runtime and application boundaries

## Decision

Installed applications are separate processes, separate source owners and
separate binary bundles. The desktop never loads installed application modules.
The current contract and migration details are in
[Applications and the desktop](../applications.md).

The desktop owns composition, Deskbar, preferences, session and device
connections. Tracker file-management views remain part of the system shells.
TikuBasic/C, Editor, Terminal, Bench, Console, Reader/Help, Image, Monitor, VFS
Inspector, calculator, hex editor and demos are app-owned implementations.

```text
system/TikuDesktop <-- public window/service protocol --> system/tiku-run
       |                                                    |
desktop/Tracker composition                            apps/<id>/<module>.so
       |                                                    |
       +---------------- public kits SDK -------------------+
```

A module is loaded only inside its own runner process. Legacy `trusted/`
installs no longer bypass this boundary. Apps may use an app-owned native
workspace through `tiku_window_app`; no pointers to desktop state cross the
process boundary. Device utilities ask the desktop's VFS service to use its
existing connection instead of opening another serial port.

## Dependency rules

1. No kit includes a `tiku_trk_*` header or application implementation.
2. No application imports desktop-private headers or source.
3. No application source or module loader links into a shipping shell.
4. Tracker core never includes X11 or Interface drawing headers. Portable
   events and definition headers are shared vocabulary.
5. Platform headers and native event types do not cross the runtime adapter.
6. A window owns its controller state and draw/event/close/destroy callbacks.
7. The desktop owns z-order, focus, input routing, invalidation and chrome.
   The application owns content, state and workers.
8. Cross-process operations use explicit public services and bounded messages,
   including metadata, clipboard, file pickers, Help and VFS access.

`make check-architecture` checks source imports, shell link lists, and linked
symbols, and tests that the application guard rejects prohibited dependencies.
This is crash/lifecycle separation, not a security sandbox.

## Library and binary shape

Seven public kits are built under `kits/`: support, device, interface,
storage, translation, application and media. Static/PIC archives are reusable
framework code, not a location for application controllers.

The system binaries are under `build/runtime/system/`. Application bundles are under
`build/runtime/apps/<id>/`, each with an `app.manifest`. Tests live in `build/tests/`
and scripted shells in `build/dev/`. Example standalone programs live in
`build/examples/`; development tools in `build/tools/`.

Tracker compiles its own sources into the desktop and browser shells; it
publishes no private application library. `core/` owns portable file-manager
behavior, `ui/` its controllers, `deskbar/` the bar, and `shell/` composition.
Shared filesystem models/backends live in Storage; image translation in
Translation. Reader and Image controllers are in `apps/`, not either kit.

## Extraction status

The former linked-in Reader/Help, Image and device utilities have been moved
into application bundles. The descriptor demo has also moved out of the SDK.
System window, startup and file-association routes resolve manifest metadata
rather than importing the applications. Stable startup IDs migrate old choices.

The public module ABI is 5. Rebuild modules and restart both sides together.
Old modules are refused with a diagnostic. The generic embedded service adapter
remains for system compatibility, but no installed-app path calls its loader.

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

## Two front-ends, one system source tree

The spatial desktop and single-window Tracker are two compositions over shared
file-management code. This does not give other applications permission to link
against either shell's private implementation.

`make print-ownership` reports ownership from the link lists;
`make print-shell-sources` reports the full inputs inspected by the app boundary
guard. Script infrastructure is linked only into development shells; the
shipping guard checks its absence from the system binaries.

The X11 migration allowlist is empty. New native imports outside the declared
platform adapters fail the architecture check. Refactors retain the feature
contract and must pass the unit and headless integration suites.
