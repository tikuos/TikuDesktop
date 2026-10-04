# Applications and the desktop

Installed applications run in separate host processes. Their sources and
binaries are not part of the desktop shell. The boundary is enforced by
`make check-architecture`, including inspection of the linked shell symbols.

## Ownership

| Source | Responsibility | Output |
| --- | --- | --- |
| `system/` | Desktop/Tracker shells, file management, Deskbar, preferences, session, device ownership | `build/runtime/system/TikuDesktop`, `TikuTracker` |
| `system/runner/` | Application process host and module loading | `build/runtime/system/tiku-run` |
| `system/supervisor/` | Desktop supervisor/restart reporting | `build/runtime/system/tiku-desk` |
| `kits/` | Public reusable SDK; no app controllers or desktop-private imports | `build/lib/libtiku_*.a`, PIC archives |
| `apps/` | Application controllers, models and resources | `build/runtime/apps/<id>/` bundles |
| `examples/apps/`, `examples/diagnostics/` | Example programs and development tools | Demo bundles, `build/examples/`, `build/tools/` |
| `tests/` | Unit and integration tests | `build/tests/`, scripted shells in `build/dev/` |

TikuBasic and TikuC share the app-owned IDE code in `apps/ide/`.
The BASIC host interpreter is built from the board's interpreter source via
`apps/basichost/` and belongs to the BASIC bundle, not the system.
Reader/Help live in `apps/reader/`; Image in `apps/image/`; PDF in
`apps/pdf/`, drawing through the PDF kit on its own workers; archives in
`apps/archive/`, read and extracted by the archive kit on its workers;
TikuDiff in `apps/diff/`, comparing through the support kit's diff on
its workers; TikuGrab in `apps/grab/`, which asks the desktop's grab
service (`TIKU_MSG_GRAB`) for the pictures, since an application cannot
see the desk; TikuSDR in `apps/sdr/`, which borrows a board's serial
port as TikuConsole does and draws the spectra the board computes;
Monitor and VFS Inspector share app-owned code in `apps/deviceutils/`.
None is linked into either shipping shell. Help requests launch Reader in its help-document mode.

For filenames and internal module boundaries, see the
[functional source guide](source-modules.md). Application tests link those
implementations through headers, without merging source files into the tests.

Tracker's file-manager views and the desktop's own preferences remain core
system features. This change does not turn every system panel into an app.

## Bundles and discovery

For example:

```text
build/runtime/
  system/TikuDesktop
  system/tiku-run
  apps/basic/
    app.manifest
    TikuBasic.so
    tiku-basic
  apps/reader/
    app.manifest
    TikuReader.so
```

The desktop reads metadata without loading executable code:

```ini
version=1
id=basic
name=TikuBasic
module=TikuBasic.so
category=programming
icon=application
types=text/x-basic,application/x-tiku-project
startup=programming/TikuBasic.so
help=apps/tikubasic.html
```

- `id` is a stable installation/startup identity.
- `module` is a leaf filename inside the bundle, not a command or path.
- `category` is `applications`, `programming`, `device-tools` or `demos`.
- `icon` names a public icon; `types` advertises file-association candidates.
  The person's File Associations preferences still select preferred handlers.
  Bundles with no document types are not offered in Open With.
- Optional `startup` is a legacy preference identity to migrate, **not** an
  instruction to auto-launch. New selections use `app/<id>`.
- Optional `help` is a default page relative to the help root. Apps may publish
  a more specific page through the SDK. Structured Help launches do not inherit
  the Reader's ordinary application-help page.

Manifests reject unknown/duplicate fields, malformed versions, unsafe module
names and overlong values. Duplicate IDs are offered only once in stable scan
order. `TIKU_APPS` can select another bundle directory. `TIKU_HELP` can select
another help root; both are resolved before the child changes directory.

Legacy flat modules and `programming/`/`trusted/` installs remain readable.
A flat module can carry `<module>.manifest`. The old `trusted/` startup
default remains for compatibility, but it no longer permits in-process loading.
The legacy `TIKU_EMBED_DEMO` switch also launches a separate demo process.

## Runtime boundary

Every launch uses a separate `tiku-run <module> [document]` process. The runner
loads the module and connects to the desktop session. The module is an
application binary, not a plugin loaded into the shell.

Applications use `tiku_app_services_t` for windows, menus, clipboard, file
pickers, Help and device requests. The desktop owns composition, window chrome,
placement, focus and input routing. Apps own their content, state and workers.
`tiku_window_app` adapts app-owned native controllers to that public protocol,
including resize/zoom metadata, menu changes and content-relative input.

Private launch messages carry bounded structured data through an inherited,
unlinked file descriptor; Help markup is not interpolated into command lines.
The runner adds the bundle directory to **its child's environment only**, so
TikuBasic can find its interpreter without installing it as a system utility.

Device Monitor and VFS Inspector use `tiku_vfs_rpc`, not a second serial
connection. The desktop resolves already-connected devices and serializes
requests with its existing device traffic. Requests are bounded; a selected
device is pinned by identity. Writes require a preceding read and compare the
current bytes before writing, under one device-worker turn. The Inspector's
review/confirmation and readback remain app-owned. Late replies are tied to a
session generation so they cannot reach a replacement application.

This is **process/crash isolation, not a security sandbox**. Applications run
under the user's host account, link the SDK, and may access host resources.
Device I/O still depends on the device backend returning within its timeouts.
Shared OS/library failures are not made impossible by this separation.

## Build, run and verify

```sh
make -j4 all
./build/runtime/TikuDesktop
# Optional supervisor:
./build/runtime/tiku-desk
# Or launch an app against an already-running desktop:
./build/runtime/system/tiku-run "$PWD/build/runtime/apps/basic/TikuBasic.so"

make check-architecture check-shipping check-style
make test
```

`make apps` builds application bundles; `make system shells` builds the system
hosts/shells; `make examples` builds demos and tools. `make run` builds the
desktop plus its loadable applications.

SDK module ABI is now **5**. Rebuild third-party modules and restart the desktop
and apps together. Old ABI modules are refused with a diagnostic, not called
through a mismatched descriptor. Known obsolete generated binaries are removed
on build; `build/TikuDesktop`, `build/TikuTracker` and `build/tiku-desk` remain
compatibility symlinks to the resource-aware runtime launchers.

Regression coverage includes manifest parsing, boundary-guard rejection cases,
startup migration, real-process launch/crash/resize, app-owned window lifecycle,
VFS RPC identity/stale-write/cancellation, and driven Help/Reader/Image/utility
interactions. Hardware-dependent suites require an explicitly selected board.

See [application diagnostics](../guides/application-diagnostics.md) for crash and stall
reports. A crashed or stalled app is reported independently of the desktop;
Force Quit and a deliberate shutdown are not classified as crashes.
