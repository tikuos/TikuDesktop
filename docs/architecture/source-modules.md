# Functional source modules

Directory ownership and compilation boundaries are separate concerns. An app
belongs under `apps/`, but its parser, window and device operations should also
be identifiable without opening a large file. The migrated families are the
shared BASIC/C IDE, Editor, Reader, Bench, Console, desktop background jobs and
desktop commands.

## Naming and interfaces

- Use `tiku_<component>_<responsibility>.c`: for example,
  `tiku_editor_file.c` or `tiku_desktop_device_stats.c`.
- A `.c` file is a separately compiled implementation, not an include fragment.
  Tests include headers and link the same implementation objects.
- Keep helpers `static` unless another module or focused regression test needs
  them. Shared private functions use their component's namespace and hidden
  symbol visibility; they are not additions to the public application ABI.
- Use narrow model headers where code needs no window/controller state. Use a
  family-private header for the existing controller state and its internal
  calls. Do not promote application-private interfaces into `kits/`.
- Group related operations rather than making one file per function. File size
  is a review signal, not a reason to split a cohesive algorithm or generated
  font/icon data. Avoid ambiguous `misc`, `utils` and `helpers` collections.

## Where to change behavior

All names below are relative to their component directory; the filename prefix
is omitted from the table for readability.

| Component | Modules and responsibilities |
| --- | --- |
| `apps/ide/tiku_ide_*` | `project`: project descriptions; `project_window`: project UI and file membership; `text`: a page's editor-kit document and view, and lines read from it; `editor`: editor windows and file lifecycle; `editor_view`: painting and geometry; `events`/`commands`: input and menu routing; `search`: find/replace and project search; `language`: BASIC normalization/renumbering; `assist`: completion and keyword help; `device`: default target, upload, BASIC limits at the run and board callbacks; `run`: local execution/build; `debug`: debugger; `console`: output/diagnostics; `recovery`: private snapshots; `splash`: splash/About; `app`: descriptor and lifecycle |
| `apps/editor/tiku_editor_*` | the text, its undo and its page are the editor kit's document and code view (`kits/editor/`); `file`: lossless reads and atomic saves; `view`: chrome painting and the code view's box; `commands`: editing and document commands; `input`: pointer/keyboard/window events; `run`: interpreter child/output; `app`: descriptor/lifecycle; `main`: standalone command-line entry |
| `apps/reader/tiku_reader_*` | `document`: bounded file reads and HTML/Markdown parsing/storage; `reading`: selection/find state; `navigation`: path policy and history; `search`: folder search and outline; `bookmarks`: bookmark storage; `images`: inline image loading; `layout`: geometry and controls; `view`: rendering and reading-tool input; `browser`: document-mode toolbar/address bar; `events`: window/menu routing; `window`: Help/document window lifecycle and generated reports; `app_host`: the desktop-process lifecycle TikuReader and Help share; `app`: TikuReader's descriptor |
| `apps/bench/tiku_bench_*` | `catalogue`: registry parsing, eligibility and selection; `device`: target/profile discovery; `runner`: argv validation, asynchronous child processes and port ownership; `results`: output parsing, result records and HTML reports; `settings`: remembered options; `layout`: geometry and controls; `view`: catalogue/results/history painting; `commands`: menus and input; `app`: descriptor/lifecycle |
| `apps/console/tiku_console_*` | `terminal`: ANSI/UTF-8 parsing, scrollback, filtering and command history; `device`: target discovery, serial I/O, port handoff and recording; `network`: SLIP framing and rootless ICMP diagnostics; `view`: geometry, controls and painting; `commands`: menus, clipboard and keyboard/pointer input; `app`: descriptor/lifecycle |
| `system/desktop/tiku_desktop_*` (background work) | `device_jobs`: polling, probing, board-run completion, property writes and replicants; `filesystem_jobs`: directory listing, file operations and volumes; `media_jobs`: thumbnails and sound connection; `device_stats`: cached device/memory information |
| `system/desktop/tiku_desktop_*` (interaction) | `folder_windows`: opening/closing spatial windows and Trash unions; `file_actions`: selection, rename, new entries/templates, destinations and progress; `drop`: drag destinations and icon drops; `navigation`: spring-loaded folder menus and incremental hints; `automation`: add-ons and scripting; `volume_commands`: mount/unmount commands; `commands`: menu dispatch, Open With, layout, query/system actions and press-and-hold menus |

`tiku_ide_project.h` and `tiku_reader_document.h` are the narrow model
interfaces. They do not import their family-private controller headers. Their
implementations are tested together without linking either app's window
controller. Editor's text model is the editor kit's document, which has suites
of its own.

The IDE is still one shared implementation built twice. `TIKU_IDE_C` applies to
every C-variant translation unit; BASIC and C do not have separate source forks.
Reader's Help and general-document modes also still share their engine. The
existing `tiku_reader_help_window_*` function names remain unchanged even though
the implementation/header are now named `tiku_reader_window`.

Desktop jobs keep their existing scheduling keys, cancellation/lifetime rules
and callbacks. The split does not add worker threads or change device traffic.
Desktop command modules keep their existing dispatcher and shared controller;
the split does not introduce another command registry. Bench and Console retain
their device-release/reclaim protocol, bounded I/O and existing settings formats.
Their functional modules use family-private state rather than claiming to be
standalone model libraries. Tests link the same separately compiled modules.

The wider desktop family retains historical `tiku_trk_desktop_*` names until
those files are substantively reorganized; there is no blanket rename.

## Builds and checks

`mk/apps.mk` defines the source families and compiles their `.c` files as separate
translation units for loadable applications. `mk/tests.mk` links the same model
and controller objects into the application regressions. The Editor standalone
entry point is excluded from both its module and unit-test links. IDE test links
exclude its optional standalone `main` through `TIKU_APP_EMBED`.

```sh
make apps                       # applications, including both IDE variants
make check-source-modules       # names, real module boundaries, model isolation
make source-report              # all-tree inventory and style audit
make test                       # existing regressions plus model/ABI checks
```

`check-source-modules` also tests the guard with invalid source layouts. It
rejects implementation includes and `.inl` files in the migrated app families,
tests that include those implementations, and model-to-window dependencies.
`test_source_models` checks the narrow model interfaces. `test_module_exports`
loads the six production app bundles, checks their IDs/descriptors and verifies
that private functions are not dynamically exported. Bench's runner fixtures
and Console's PTY/SLIP tests run without a physical device.

The source inventory and report include `.inl` and Objective-C files in apps,
kits and system. The older strict style gate remains scoped to the system
families: `source-report` exposes app/kit style debt rather than grandfathering
it silently or making an unrelated formatting rewrite necessary. Larger kit
algorithms, remaining utility-app fragments and the IDE's long event handler remain
visible follow-up candidates, not claimed as resolved by this extraction.

## Compatibility

There are no changes to executable/module filenames, manifests, app IDs,
startup associations, user settings, device protocols or public kit APIs.
Internal source paths changed intentionally. Runtime processes remain separate;
source splitting does not move app logic back into the desktop binary.
