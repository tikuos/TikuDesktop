# Documentation

## Current architecture and development

- [Repository and output layout](architecture/layout.md): ownership, build products and compatibility paths.
- [Architecture book](architecture/BOOK.md): kits, shells, applications and board interfaces.
- [Application architecture](architecture/applications.md): process separation, manifests and public SDK.
- [Tracker architecture](architecture/tracker/README.md): file-manager internals and design references.
- [Development guide](guides/development.md): workflow, contracts and build/test pitfalls.
- [Application diagnostics](guides/application-diagnostics.md): application crash and stall reports.
- [Tracker and desktop guides](guides/tracker/): preferences, readers, device utilities and scripting.
- [Tracker behavior contract](contracts/tracker.md): the executable feature/fidelity checklist.
- [Tests](../tests/README.md) and [examples](../examples/README.md).

Public-kit documentation stays beside each kit under `kits/`; application
and board implementation notes stay beside their code. The pages displayed
inside TikuDesktop live in `assets/help/` and are packaged with the runtime.

## Historical material

`plans/` contains design proposals and task plans. `history/` contains dated
notes and change records. These describe their original context and may use
older paths; they are not the source-layout authority. Use the layout guide
and Makefiles for the current structure.
