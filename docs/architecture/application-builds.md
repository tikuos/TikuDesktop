# Independent application builds

The desktop, application SDK and end-user applications have separate build
entry points. System implementation lives in `system/`, reusable kits in
`kits/`, and application implementation in `apps/`. Applications run in
separate processes through `tiku-run` and use the public kit APIs.

## Build the desktop and SDK

Run these commands from the desktop repository root:

```sh
make -j6 core
make -j6 sdk
make install-sdk SDK_PREFIX="$HOME/.local/share/native-desktop-sdk"
```

`core` builds the production desktop, application runner, supervisor and
runtime resources. It does not compile applications, examples or tests.
The resulting launcher is `build/runtime/TikuDesktop`.

`sdk` exports public headers, position-independent kit archives,
redistribution notices, ABI information and build metadata to `build/sdk/`.
`install-sdk` copies that export to `SDK_PREFIX`. Set `SDK_DIR` to change
the export directory. An export or installation refuses to replace a
directory without an SDK marker.

The SDK contains no system or application implementation sources. Its
`sdk.mk` and `lib/pkgconfig/*.pc` resolve paths relative to the installed
SDK, so the exported directory can be moved. Private headers and headers
that include them are excluded. The SDK records the application ABI,
host operating system, architecture and available kits.

The SDK targets the host and kit configuration it was built with. Building
an application also requires a C toolchain, Make, Python 3 and the native
development dependencies recorded in the SDK compiler and linker flags.
The installed runtime needs the corresponding host libraries. Static kit
archives are linked into each module; updating those archives requires
rebuilding the application to include their changes.

## Build applications

The application subtree uses an exported SDK. It does not invoke the
desktop build:

```sh
make -C apps list
make -C apps -j6 SDK="$HOME/.local/share/native-desktop-sdk" APP="calc editor"
make -C apps/editor -j6 SDK="$HOME/.local/share/native-desktop-sdk"
```

`make -C apps` builds the production application inventory when an SDK is
available. Its default SDK location is `../build/sdk` relative to `apps/`.
A missing SDK produces an error with instructions. Each application source
directory has a Makefile; directories that implement multiple applications
accept `APP` to select an application ID.

Application modules and manifests go under `apps/build/apps/<id>/`.
Intermediate objects, dependency files and application archives go under
`apps/build/`. Set `APP_BUILD` and `APP_OUTPUT` to select other locations.
`make -C apps clean` removes a marked application build directory.

`apps/catalog.mk` owns the production source lists, kit dependencies,
compile definitions and shared application libraries. `apps/rules.mk`
compiles those definitions in both the root build and the standalone
application build. Shared reader and office code builds
into application archives when a consumer requires it. These archives
belong to applications and are not part of the SDK.

Compiler dependency files track included headers and `.inl` files. Command
stamps track compiler and flag changes. An unchanged build preserves
objects and modules; an application edit does not rebuild the desktop.

The root convenience command exports an SDK and builds one application:

```sh
make app-build APP=editor
```

## Build outside the desktop checkout

Export the application and the shared application sources it requires:

```sh
make app-source APP=editor
cp -R apps/build/sources/editor /tmp/native-editor
make -C /tmp/native-editor -j6 SDK="$HOME/.local/share/native-desktop-sdk"
```

The source export contains its Makefile, build catalog and application
sources. The build requires the exported SDK and host development tools;
it does not require the desktop checkout. Its default application is the
one selected during export.

## Package and install an application

```sh
make -C /tmp/native-editor SDK="$HOME/.local/share/native-desktop-sdk" package
make package-core
make -C /tmp/native-editor SDK="$HOME/.local/share/native-desktop-sdk" install \
    APP_INSTALL_DIR=/path/to/desktop/dist/TikuDesktop-core/apps
```

`package` creates `build/packages/<id>/` beneath the application source
export. The bundle contains the module, manifest, SDK redistribution
notices and application metadata. Set `APP_PACKAGE_DIR` to select another
package directory. `install` copies that bundle into `APP_INSTALL_DIR`.
It replaces an existing bundle only when the bundle carries the generated
application marker. Restart the desktop to refresh its application list.

`package-core` creates `dist/TikuDesktop-core/` with system programs and
resources and an empty application directory. It does not compile apps.
`make check-core` validates that distribution. The full distribution
continues to use `make package`, which includes the production apps.
`make app` retains its macOS Finder-bundle meaning.

The default root `make` remains the development build, including apps,
examples and tests. `make apps` builds application products and their kit
prerequisites within the root build. Neither is required to compile an
exported application.

## Verification

```sh
python3 tools/test_app_build.py
make check-app-build
```

`check-app-build` exports an SDK and tests application sources built in a
separate temporary directory against a relocated SDK. It checks module
loading and ABI, compiler dependencies, unchanged rebuilds and application
installation. The application runtime lane uses the scripted development
desktop with independently packaged applications; `make check-package`
checks the shipping distribution. It also checks the core-only graph with application sources
absent and the core-only package inventory.

TikuDesktop retains its device and programming applications. The BASIC
host interpreter is an optional helper compiled by the root build from
the firmware checkout selected by `TIKUOS`. To build that helper independently:

```sh
make -C apps/basichost TIKUOS=/path/to/tikuOS
make -C apps/ide APP=basic SDK="$HOME/.local/share/native-desktop-sdk" TIKUOS=/path/to/tikuOS package
```

The second command includes the helper in the BASIC package. BASIC source
exports include its host adapter and build rules; they require the explicit
firmware checkout to compile the optional interpreter. Application modules
use the SDK; the helper depends on firmware interpreter sources.
