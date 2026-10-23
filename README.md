# TikuDesktop

TikuDesktop is a desktop for Linux and macOS with applications and TikuOS board
integration. Its repository is `https://github.com/tikuos/TikuDesktop.git`.
Shared kits, matching applications and shell components come from
[Stomme](https://github.com/tikuos/Stomme).

## Where things belong

| Directory | Contents |
| --- | --- |
| `foundation/` | Stomme Git submodule pinned to a reviewed commit |
| `system/` | Product shell configuration and board extensions |
| `apps/` | End-user applications; each runs in a separate process through the public kits |
| `kits/` | Root build compatibility entry point and product icon registration |
| `examples/` | Demonstration applications and diagnostic galleries; not production apps |
| `tests/` | System, kit, application and integration tests, fixtures, test drivers |
| `assets/` | Help, translations, application artwork, sounds and icon sources |
| `board/` | Optional firmware-side GUI bridge and board examples |
| `tools/` | Build helpers, source checks, asset generators and developer tools |
| `mk/` | Desktop, kit and development build fragments |
| `docs/` | Architecture, guides, contracts, historical plans and notes |

See [the layout guide](docs/architecture/layout.md) for the detailed tree,
old-to-new paths, and compatibility rules. `TikuTracker/` contains only a
compatibility Makefile; it no longer owns the desktop, tests or assets.
The [functional module guide](docs/architecture/source-modules.md) maps source
filenames to responsibilities within the IDE, Editor, Reader, Bench, Console,
desktop jobs and desktop commands.

Shared implementations come from the pinned Stomme submodule. Recursive cloning
initializes it. A local development checkout can be selected with
`FOUNDATION_ROOT=/path/to/Stomme` or an ignored `foundation.local.mk`. See [source ownership](docs/architecture/foundation.md) for the
foundation, SDK and product contribution commands.

## Clone and build

```sh
git clone --recurse-submodules https://github.com/tikuos/TikuDesktop.git
cd TikuDesktop
sh hygiene/install_hooks.sh
```

For an existing clone, run `git submodule update --init --recursive` after
checking out a product revision. Git records Stomme's exact commit at
`foundation/`; `foundation.lock.json` checks its selected source-content hash.
Follow [the contribution rules](AGENTS.md) and Stomme's
[commit guide](https://github.com/tikuos/Stomme/blob/main/hygiene/commit-style.md).


On Linux, install a C toolchain, Make, Python 3 and X11 development libraries.
macOS uses the native Cocoa window backend and the Xcode command-line tools;
it does not require an X server. Python 3 must also be available.

```sh
make -j8                 # complete development build, including tests and demos
make run                 # build and launch TikuDesktop
make core                # desktop and system hosts, without compiling apps
make sdk                 # export the application SDK to build/sdk/
make runtime             # production binaries, application bundles and assets
./build/runtime/TikuDesktop
./build/runtime/TikuDesktop -d -p /dev/ttyACM0   # board over serial
./build/runtime/TikuDesktop -fullscreen
```

The main executable names, application IDs, settings and module ABI are
unchanged. `./build/TikuDesktop` and `./build/tiku-desk` remain
compatibility launchers. The single-window TikuTracker is built only for
the test harness, under `build/dev/`: the file manager does not start
without the desktop. `TikuTracker/` and `kits/` forward to the root build.
The `apps/` subtree builds independently against an exported SDK; see
[independent application builds](docs/architecture/application-builds.md).

The optional `tiku-basic` host interpreter uses the firmware interpreter source.
Set `TIKUOS=/path/to/tikuOS` to select it. The parent directory is the default
for checkouts inside a firmware tree. Without that source, the helper is skipped;
the desktop, SDK, application modules and comment checks remain available.

To provide board-side applications to a TikuOS firmware checkout, clone this
repository recursively into its ignored `applications/` directory. The board
overlay compiles shared GUI models from `applications/foundation/kits/`.
See [board applications](board/README.md).

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
use X11 on macOS; rebuild when changing the host backend.

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
