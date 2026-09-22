# TikuDesktop

TikuDesktop, its applications, and its reusable UI/device kits for Linux and
macOS. This is an independent repository, normally checked out at
`tikuOS/applications/`.

## Where things belong

| Directory | Contents |
| --- | --- |
| `system/` | Desktop and Tracker shells, Deskbar, shared shell services, application runner, supervisor |
| `apps/` | End-user applications; each runs in a separate process through the public kits |
| `kits/` | Seven reusable libraries: support, device, storage, interface, translation, application, media |
| `examples/` | Demonstration applications and diagnostic galleries; not production apps |
| `tests/` | System, kit, application and integration tests, fixtures, test drivers |
| `assets/` | Help, translations, application artwork, sounds and icon sources |
| `board/` | Optional firmware-side GUI bridge and board examples |
| `tools/` | Build helpers, source checks, asset generators and developer tools |
| `mk/` | The single build's Makefile fragments |
| `docs/` | Architecture, guides, contracts, historical plans and notes |

See [the layout guide](docs/architecture/layout.md) for the detailed tree,
old-to-new paths, and compatibility rules. `TikuTracker/` contains only a
compatibility Makefile; it no longer owns the desktop, tests or assets.
The [functional module guide](docs/architecture/source-modules.md) maps source
filenames to responsibilities within the IDE, Editor, Reader, Bench, Console,
desktop jobs and desktop commands.

## Build and run

On Linux, install a C toolchain, Make, Python 3 and X11 development libraries.
macOS uses the native Cocoa window backend and the Xcode command-line tools;
it does not require an X server. Python 3 must also be available.

```sh
make -j8                 # complete development build, including tests and demos
make run                 # build and launch TikuDesktop
make runtime             # production binaries, application bundles and assets
./build/runtime/TikuDesktop
./build/runtime/TikuDesktop -d -p /dev/ttyACM0   # board over serial
./build/runtime/TikuDesktop -fullscreen
```

The main executable names, application IDs, settings and module ABI are
unchanged. `./build/TikuDesktop`, `./build/TikuTracker` and
`./build/tiku-desk` remain compatibility launchers. Running `make` inside
`TikuTracker/`, `apps/` or `kits/` still forwards to this build.

The optional `tiku-basic` host interpreter is compiled from
`../kernel/shell/basic/tiku_basic.c`. Set `TIKUOS=/path/to/tikuOS` if the
firmware checkout is elsewhere; without it, that helper is skipped with a
message and the other products still build.

## Generated output

```text
build/
  runtime/       runnable desktop: launchers, system/, apps/, assets/
  lib/           SDK archives; position-independent archives in lib/pic/
  obj/           intermediate objects and dependency files
  generated/     generated headers
  dev/           script-enabled shells and the external test driver
  tests/         unit-test executables and fault-injection modules
  examples/      standalone demos and diagnostic galleries
  tools/         compiled development utilities
dist/
  TikuDesktop/       portable production folder (make package)
  TikuDesktop.app/   native Finder bundle (make app on macOS)
```

Use the top-level runtime launchers rather than copying a binary out of
`system/`; the launchers locate the matching apps, help and translations.
`build/runtime/` can contain demos after a development build. Distributions
are assembled from production source manifests, excluding demos, test
fixtures, stale bundles, object files and SDK archives.

```sh
make package
./dist/TikuDesktop/TikuDesktop
# macOS only:
make app
open dist/TikuDesktop.app
```

The Finder bundle includes apps, help and translations and is signed ad hoc
when signing is available. `build/TikuDesktop.app` remains an alias. A
pre-existing bundle at that old path is preserved as
`build/TikuDesktop.app.legacy-backup` when the alias is created.

`make clean` removes `build/`, not `dist/` or user preferences. Packaging
replaces only an output carrying its generated-package marker; an unknown
directory at the destination is refused. Packages are relocatable folders
for the host they were built on, not cross-platform or statically linked
installers; Linux still needs the host X11/runtime libraries.

## Verification

```sh
make test                 # boundaries, unit tests, BASIC checks, driven UI tests
make check-layout         # build migration, packaging and launcher regressions
make check-source-modules # functional source boundaries and model isolation
make source-report        # inventory/style audit, including apps, kits and .inl
make check-architecture   # ownership, SDK boundaries and layout checks
make check-shipping       # no test interpreter in production shells
make check-package        # build and validate the production distribution
make lint                 # repository style and available house comment checks
TRK_SHELL_ONLY=about.script sh tests/integration/shell/run.sh
```

The driven UI tests use isolated homes and offscreen rendering. On Linux,
native-window tests use Xvfb or Xephyr when installed. Hardware lanes are
opt-in: `TRK_PORT=/dev/ttyACM0 make test`. Set `TIKU_HOST=x11` explicitly to
use X11 on macOS; use a clean build when changing the host backend.

Production shells in `build/runtime/system/` refuse `-script`. The test
interpreter exists only in the `build/dev/` versions. See
[testing](tests/README.md) for ownership and test commands.

## Documentation

Start with the [documentation index](docs/README.md), the
[layout guide](docs/architecture/layout.md), or the
[architecture book](docs/architecture/BOOK.md). Application process isolation,
manifests and the public SDK are covered by
[application architecture](docs/architecture/applications.md).

The local `_attic-*` directory, when present, is an archive outside the active
build. It is neither migrated nor packaged.
