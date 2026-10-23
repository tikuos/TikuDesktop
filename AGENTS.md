# AGENTS.md

TikuDesktop follows the TikuOS desktop comment, prose and commit conventions.
The shared contracts are in `foundation/comment-style.md`,
`foundation/prose-style.md` and `foundation/hygiene/commit-style.md`.

## Source ownership

`foundation/` is the pinned Stomme Git submodule. It owns base kits, the optional
basic Document provider, shared application implementations and common shell
components. Product resources, configuration headers, application manifests,
retained variants and board extensions belong to this repository. The composed
`build/foundation/` view records selected source owners. Do not restore migrated
shared implementations into product source directories.

Edit shared code in a Stomme checkout and publish it there. Adopt a reviewed
commit by updating the `foundation/` gitlink and `foundation.lock.json` together.
`FOUNDATION_ROOT` or an ignored `foundation.local.mk` selects a development
checkout. SDK and detached application changes require matching source/provider
identities. Keep public C and protocol names stable.

## Build and checks

Initialize dependencies with `git submodule update --init --recursive`. Run
`sh hygiene/install_hooks.sh` after cloning. Use `make -j6 core sdk apps shells`,
`make lint check-architecture check-shipping check-tools check-font check-toolkit`,
`make check-package check-core` and the affected regression suites. SDK changes
require the relocated HTML example and `make check-app-build`. The full product
suite is `make test`; it includes desktop workflows.

Use one Make graph per output directory. Do not edit C/header inputs during
compilation. Run desktop scripts and system suites serially across products.
New regression guards must fail against their defective input before accepting
a passing result. Stage new source before tracked-source lint. Review changed
comments against their implementation; mechanical checks do not establish truth.

TikuOS firmware source is optional for the desktop. `TIKUOS=/path/to/tikuOS`
selects the BASIC host interpreter source. Board-side applications use the
firmware overlay when this checkout is placed at `tikuOS/applications/`.
Open device ports or flash hardware only when explicitly requested.

## Commits

Commit one capability at a time with `Area: what changed` within 72 characters
and at most three plain English body bullets. Present the subject, configured
author and body and wait for approval before committing. Use plain `git commit`
as the configured identity. Do not use author overrides, assistant trailers or
`--no-verify`. Push when requested and check remote containment before amending.
Stomme and TikuDesktop commits belong in their own repositories.

Local plans, captures and result ledgers stay outside published source.
