# Examples and diagnostics

`apps/` contains small SDK applications and their shared framework. Start with
[the example application guide](apps/README.md). `diagnostics/` contains the
headless probe, desktop gallery, sound gallery and larger demo host.

`make examples` builds standalone programs in `build/examples/` and demo
modules under `build/runtime/apps/`. The complete `make` development build
also builds them. `make package` deliberately excludes these demo apps.

Application fixtures designed to crash or stall are tests, not examples;
they live under `tests/fixtures/`. The metrics generator is a developer tool
under `tools/`, with output in `build/tools/`.
