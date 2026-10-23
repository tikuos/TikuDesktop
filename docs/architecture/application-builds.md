# Independent application builds

The desktop, application SDK and end-user applications have separate build
entry points. Shared implementation comes from the foundation dependency;
product configurations, application catalogs, descriptors and retained variants
stay in this repository. Applications run in separate processes through
`tiku-run` and use public kit APIs. See [source ownership](foundation.md) for
selected source paths and shared contribution commands.

## Build the desktop and SDK

Set `FOUNDATION_ROOT` to the foundation checkout selected by
`foundation.lock.json`. The default is `foundation/` under the product root.
An ignored `foundation.local.mk` can assign `FOUNDATION_ROOT` for local work.
The dependency location does not require a sibling product checkout.

Run these commands from the desktop repository root:

```sh
make -j6 FOUNDATION_ROOT=/path/to/foundation core
make -j6 FOUNDATION_ROOT=/path/to/foundation sdk
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
host operating system, architecture, available kits, foundation content revision,
document provider and SDK identity. Header and archive fingerprints detect
mixed SDK artifacts before application compilation. Set
`APP_DOCUMENT_PROVIDER` to require a particular provider for an independent
application build.

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
stamps track compiler, flag and SDK identity changes. Each linked module has
an adjacent `.sdk.json` file recording its SDK metadata and module fingerprint.
The runtime checks the module's embedded SDK identity before opening it;
incompatible or unmarked modules require rebuilding with the product SDK. An unchanged build preserves
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

## Shared source selection

`foundation.product.json` declares the document provider, product overrides and
excluded copies. `build/foundation/` holds a generated source view whose shared
paths point to the foundation. The basic document provider points to the
foundation optional package; advanced document and Deck sources point to the
Kintsugi product tree.
The build compiles the selected files into one implementation per kit and
configuration. `sources.json` records the selected origins and byte hashes;
`identity.json` records the foundation revision, provider, target and flags.
Existing source copies under `kits/` remain outside the shared compilation
selection. Source-including regressions use the selected include roots.

`foundation.lock.json` pins source content. A mismatch stops the build.
`FOUNDATION_ALLOW_DIRTY=1` permits a recorded development override. Changes to
the foundation source, providers or flags update the build identity and
invalidate compiled consumers. A content pin supports a local unpublished
foundation; publication requires the repository's chosen name and remote.

## Optional document and presentation kits

The desktop and general Translation archive link base kits. Document editing
and persistence use the selected `document` and `document_formats` archives.
Kintsugi supplies `deck`, `deck_formats` and `office_shapes` for presentation
editing, conversion and full Office shape geometry. Application catalog entries
request those dependencies explicitly.

The SDK exports the selected provider headers with its archives and fingerprints.
An archive or header from another SDK fails validation before compilation.
Private kit headers are excluded. The base pkg-config descriptor links base
kits; optional `tiku-document`, `tiku-document_formats`, `tiku-deck`,
`tiku-deck_formats` and `tiku-office_shapes` descriptors are present when those
packages are exported. Independent builds stop if a selected app needs a kit
absent from the SDK. Shell interfaces use public runtime and Interface types.

## Shared application implementations

The foundation application package supplies matching implementations and tests.
The product's application list, manifests and `apps/shared/tiku_app_product.h`
supply names, icon registration and existing data-file defaults. Root builds
compile `build/foundation/apps`; independent app builds compose the same sources
under their selected application build directory. Source exports copy the
selected files and configuration and require an exported SDK to compile.

Editor keeps its Run and window variants; Notes keeps its window/input variant.
Write and the Office viewer retain their basic and advanced implementations.
Board tools and Word remain product-owned. The active ownership inventory is
`build/foundation/application-sources.json`. Inactive source copies are excluded
from compilation and source exports.


## Shared shell sources and product defaults

The `shell` selection in `foundation.product.json` enables the foundation desktop
package. Core and development shells compile `build/foundation/system`, whose
`shell-sources.json` records each implementation and product extension. The
runner and supervisor use the same selected source view and public kit APIs.

`system/product/tiku_shell_product.h` provides product names, executable and
resource paths, settings locations, About text, initial file types and association
labels. It also declares handler capacity and the board, extended remote-service
and Notes-tab switches. The existing settings and application-data paths remain
available. Application inventories and assets stay in the product repository.

TikuDesktop retains its board run, job, tray, preference and VFS extensions;
Kintsugi retains its host panel jobs and enables its remote text, printing,
accessibility, link and Notes-window behavior. Production shells link the
script-refusal stub, and development shells link the script interpreter.
Source-selection changes invalidate cached object configuration. Boundary checks
inspect the selected source view.
