# Foundation source ownership

The product selects the Stomme shared framework through its pinned `foundation/`
Git submodule and a source-content lock in
`foundation.lock.json`. `foundation.product.json` selects providers and packages.
Set `FOUNDATION_ROOT` on the Make command line or in an ignored `foundation.local.mk`. Its default is
`foundation/` under the product root. No sibling product checkout is required.

```sh
make -j6 FOUNDATION_ROOT=/path/to/foundation core sdk apps shells
make check-architecture check-shipping check-font check-tools
make check-package check-core
```

The foundation owns the base kits, the optional basic Document Kit, matching
application implementations, common shell implementations and their shared
tests. Products own application descriptors, resources, catalogs, configuration
headers and retained application variants. Board extensions belong to
TikuDesktop; advanced Document, Deck and Office Shapes providers belong to
Kintsugi. The selected provider archives remain separate from the desktop core.

`mk/foundation.mk` composes sources under `build/foundation/` before Make reads
its source lists. `sources.json`, `resources.json`, `application-sources.json`
and `shell-sources.json` record the selected paths and owners. Paths such as
`kits/interface/tiku_window.c` and `system/shared/core/` in architecture guides
are logical paths in that view. Migrated implementations are absent from the
product source directories. Build checks reject restored copies, undeclared
application dependencies and incompatible providers.

Edit shared code in the foundation checkout. Run its `make check test-tools`,
print the content revision with `python3 tools/product_bridge.py --hash`, and
update `source_sha256` in each adopting product lock. Then rebuild each product,
its SDK and application modules. An SDK identity change requires rebuilding
detached applications too. Do not change shared source during a consumer build.

`make sdk` exports selected public headers, PIC archives, provider metadata and
redistribution notices from the composed view. `make app-source APP=editor`
exports a source bundle with its required shared application libraries and
product configuration. That export builds against a relocated SDK without a
product or foundation source checkout. Desktop packages contain the product's
declared application inventory; core packages contain no applications.

`make -C kits` and the retained Tracker Makefile forward to the product root.
They remain compatibility build entry points. `build/runtime/` contains current
runtime launchers; `build/dev/` contains test shells. Generated archives and
objects belong in `build/`, not in source directories.

For a dependency rollback, restore the preceding foundation source and lock,
then rebuild the desktop, SDK and application modules together. Preserve user
preferences and documents outside build output. SDK identity checks reject
modules built against the other revision. Reopening saved documents and checking
preference values establish compatibility for the rehearsed update; schema
changes require their own downgrade policy.

The foundation's `docs/contracts/maintenance.md` defines shared contribution
review, downstream checks, release identifiers and ownership of product failures.
A release record includes the product source snapshot, shared content pin,
provider profile, SDK identity, toolchain/dependency versions and package hashes.
Native macOS runtime and bundle qualification requires execution on macOS.

## Git dependency updates

The canonical repositories are `https://github.com/tikuos/TikuDesktop.git` and
`https://github.com/tikuos/Stomme.git`. TikuDesktop records Stomme's exact commit
as the `foundation/` gitlink. Its lock records the foundation identity, repository
URL and selected source-content SHA-256. These checks preserve the product's
chosen framework revision across clones.

Publish a shared change in Stomme first. In the product checkout, fetch Stomme,
check out the reviewed commit, print its content hash and update the lock:

```sh
git -C foundation fetch origin
git -C foundation checkout --detach <reviewed-commit>
python3 foundation/tools/product_bridge.py --hash
```

Set `source_sha256` to that hash and run the product build, SDK and runtime
checks. Commit `foundation` and `foundation.lock.json` together after message
review. Each product chooses when to adopt a framework revision. Do not advance
the dependency by following a moving branch during a release build.

A local `FOUNDATION_ROOT` override supports shared development. Its contents
must match the lock, or `FOUNDATION_ALLOW_DIRTY=1` records a development override.
It does not change the committed submodule pin. Detached application source
exports use their SDK and require no Stomme checkout.
