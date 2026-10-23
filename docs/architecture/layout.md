# Repository and build layout

The repository has three distinct kinds of content: maintained source,
generated development output, and distributable runtime output. Root builds
use `build/`; standalone application builds use `apps/build/` by default.
Both output directories are excluded from version control.

## Selected foundation sources

Shared kit, application and shell implementations live in the foundation
checkout. This product retains configurations, descriptors, resources, provider
implementations and application variants. The paths in the source map below
refer to the composed `build/foundation/` tree for migrated sources. See
[foundation source ownership](foundation.md) for build and contribution commands.

## Source ownership

```text
applications/
  system/
    desktop/       spatial desktop shell and desktop-owned preferences
    tracker/       single-window Tracker shell
    deskbar/       Deskbar, desktop icons and their preferences
    shared/
      core/        shared file models, launch services and state
      ui/          shared file-manager views, dialogs and overlays
      shell/       session, shell adapters and test-interpreter seam
    runner/        tiku-run: isolated application process host
    supervisor/    tiku-desk: desktop process supervisor
  apps/            application implementations and production manifests
  kits/            public SDK, including the UI toolkit and host backends
  examples/
    apps/          small SDK applications and their shared demo framework
    diagnostics/   probe and visual/sound galleries
  tests/
    system/        desktop, Tracker and shared-service unit tests
    kits/          public SDK unit tests
    apps/          application unit tests
    integration/   cross-component and scripted UI tests
    fixtures/      test data, board stubs and fault/stall helpers
    tools/         external UI-test driver source
  assets/          runtime resources, artwork and icon source files
  board/           firmware-facing GUI bridge, kept out of the host core
  tools/           developer checks, generators and packaging scripts
  mk/              build rules split by responsibility
  docs/            architecture, guides, contracts, plans and history
```

`system/shared/` is private to the system shells. It is not a second public
SDK. Applications include the public `kits/` headers and communicate with
desktop services across the existing application IPC boundary. They do not
link desktop controllers, and the desktop does not load application modules
into its own process. `make check-architecture` enforces these boundaries.

Basic and C intentionally share `apps/ide/`; Monitor and VFS Inspector share
`apps/deviceutils/`. These are coherent shared implementations, not two
copies of each app. The BASIC interpreter host remains `apps/basichost/`
because it builds against the optional firmware checkout.

`apps/calculator/` is the ordinary calculator. `apps/developer-calculator/`
is the programmer's calculator. Runtime IDs remain `calc` and `calculator`,
respectively: readable source directory names do not rename saved app IDs.

Within those directories, the [functional module guide](source-modules.md)
maps filenames to responsibilities and describes private interfaces and the
source checks. IDE, Editor, Reader, Bench and Console implementations compile as
separate modules rather than including `.inl` implementation fragments.

## Runtime and development products

The one root Makefile includes `mk/toolchain.mk`, `kits.mk`, `system.mk`,
`apps.mk`, `tests.mk`, `bundle.mk` and `package.mk`. It owns a single dependency
graph, including parallel builds. `make` builds the complete development
tree; `make runtime` builds the shipping binaries, applications and resources.

| Output | Purpose |
| --- | --- |
| `build/runtime/system/` | TikuDesktop, tiku-run and tiku-desk |
| `build/runtime/apps/<id>/` | Stable app ID, manifest, module and optional helper/standalone binary |
| `build/runtime/assets/` | Runtime artwork, sounds, help and translation catalogs |
| `build/runtime/TikuDesktop`, `tiku-desk` | Resource-aware launchers |
| `build/dev/` | The harness's shells with the script engine, TikuTracker among them: the single-window file manager is test infrastructure, never a product |
| `build/lib/`, `build/lib/pic/` | SDK static and position-independent archives |
| `build/obj/`, `build/obj/pic/` | Intermediate object/dependency files |
| `build/generated/` | Generated headers |
| `build/dev/` | Script-enabled shells and trk-conduct |
| `build/tests/` | Unit tests and deliberately faulty application fixtures |
| `build/examples/`, `build/tools/` | Demonstrations and development tools |
| `dist/TikuDesktop/` | Relocatable production runtime; no demos or test infrastructure |
| `dist/TikuDesktop.app/` | macOS Finder bundle made from the production runtime |

The runtime launchers resolve assets relative to their own location, including
when reached through compatibility symlinks. `TIKU_APPS`, `TIKU_HELP` and
`TIKU_STRINGS` remain explicit overrides. A Finder bundle supplies its own
Resources paths, as before for applications.

`make package` uses `apps/*/*.app` as its production inventory. It requires
each module, copies only declared modules/manifests and known helper binaries,
and assembles fresh assets from source. A stale directory left in `build/`
cannot become a release application. The optional BASIC host is included
when available. Packaging validates the staged tree before publication:
launchers and programs must be executable, every runtime resource must be
present, and the app inventory must match the source manifests. Unlisted
bundles, extra files inside a bundle, development files and symlinks are
rejected. `make check-package` repeats validation on the published package
and exercises Help and translation loading from a relocated copy.

Packaging stages a complete new tree before replacing a previous generated
package. It refuses an unmarked destination and retains the previous package
if staging fails. `dist/` is ignored by Git and is not removed by `make clean`.
It is still generated output: do not keep personal documents inside it.

## Compatibility and migration

Executable names, module filenames, app IDs, startup identities, file
associations, settings, device protocols and the application ABI are unchanged.
No user home or preferences migration is performed.

| Earlier source location | Current location |
| --- | --- |
| `TikuTracker/src/shell/desktop/` | `system/desktop/` |
| `TikuTracker/src/shell/tracker/` | `system/tracker/` |
| `TikuTracker/src/{core,ui}/` | `system/shared/{core,ui}/` |
| Remaining `TikuTracker/src/shell/` | `system/shared/shell/` |
| `TikuTracker/src/deskbar/` | `system/deskbar/` |
| `system/run/`, `system/desk/` | `system/runner/`, `system/supervisor/` |
| `apps/edit/`, `apps/term/` | `apps/editor/`, `apps/terminal/` |
| `apps/calc/`, `apps/calculator/` | `apps/calculator/`, `apps/developer-calculator/` |
| `apps/examples/`, `apps/samples/` | `examples/apps/`, `examples/diagnostics/` |
| `TikuTracker/tests/`, `apps/tests/` | `tests/`, organized by ownership |
| `apps/fault/`, `apps/stall/` | `tests/fixtures/fault/`, `tests/fixtures/stall/` |
| `TikuTracker/help/`, `TikuTracker/strings/` | `assets/help/`, `assets/strings/` |
| `TikuTracker/tools/` | `tools/`, `tools/icons/`; HVIF sources in `assets/icons/hvif/` |
| Root book/plans, Tracker specs/guides | `docs/architecture/`, `docs/plans/`, `docs/guides/` |

The old `TikuTracker/Makefile` remains a forwarding entry point. It contains
no competing source tree or build. `kits/Makefile` forwards to the root build. `apps/Makefile` builds
applications against an exported SDK; see
[independent application builds](application-builds.md).

`tools/prepare_build.py` moves recognized legacy generated directories into
their canonical locations without overwriting occupied destinations. When
old and new paths both exist it stops with an explanation; preserve one
tree and rerun instead of guessing which is current.

Manifest installation and obsolete-output cleanup both depend on migration
completing first, including with `make -j`. The layout tests exercise these
Makefile rules against a legacy output tree with delayed migration.

The build root holds the two launch paths people type, `build/TikuDesktop`
and `build/tiku-desk`, as links into `build/runtime/`,
and nothing else of the earlier layout: the archives live in `build/lib/`
(`lib/pic/` for the PIC ones), the objects in `build/obj/`, the generated
headers in `build/generated/`, the shells and modules in `build/runtime/`,
the script-capable shells in `build/dev/`, the tests, tools and examples in
their own directories. The aliases an earlier layout left beside them
(`build/system`, `build/apps`, `build/gen`, `build/pic`, `build/libtiku_*.a`,
`build/icon-export`, and `build/TikuTracker`, the standalone file manager
nothing but the harness starts) are removed by `tools/prepare_build.py` on the next
build; scripts use the canonical paths. A macOS bundle formerly at
`build/TikuDesktop.app` is preserved under a `.legacy-backup` suffix before
being replaced by an alias to `dist/TikuDesktop.app`.

Existing local attic/archive directories are deliberately outside this
change. They are not inputs to the build or package.

## Adding or moving a component

1. Put reusable public functionality in its owning kit; shell-private code
   belongs under `system/`, app behavior under `apps/`.
2. Put production metadata in the app's `.app` manifest and add its module
   descriptor in `apps/catalog.mk`. Do not change an established ID for a directory rename.
3. Put examples under `examples/`, not `apps/`. Deliberately crashing helpers
   belong in `tests/fixtures/` and must never be production manifests.
4. Add unit tests under the appropriate `tests/` group. Test basenames must
   be unique; Make discovers them automatically. Extra dependencies belong
   in `mk/tests.mk`.
5. Run `make test`, `make check-shipping`, `make check-layout` and
   `make check-package`. Native macOS bundle execution must be checked on
   macOS; Linux layout tests only validate the bundle assembly contract.
