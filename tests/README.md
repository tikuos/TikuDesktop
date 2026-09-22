# Tests

Unit suites are grouped by the component they exercise: `system/`, `kits/`
and `apps/`. Cross-component tests and driven shell scripts live under
`integration/`. Data, stub headers, fault-injection modules and helper servers
live under `fixtures/`; the external `trk-conduct` driver source is in `tools/`.

`mk/tests.mk` discovers `test_*.c` directly under the four groups. Basenames
must be unique. The binaries retain their original names in `build/tests/`.
Application framework `test_app` has its own link rule in `mk/apps.mk` and
is included in the test run. Host-specific suites are selected by `TIKU_HOST`.

```sh
make tests                            # compile the suites
make test                             # full host/unit/integration run
./build/tests/test_editor_resize       # one compiled suite
make check-layout                     # Python layout/packaging regressions
TRK_SHELL_ONLY=help.script sh tests/integration/shell/run.sh
```

Run suites from the repository root. The script harness resolves its own
source paths and supplies temporary homes, fixtures, app bundles and catalogs.
It also tests production shells through the external conductor. Production
executables do not contain the development `-script` interpreter.

Hardware tests are opt-in via `TRK_PORT`. Linux native-window tests need
Xvfb or Xephyr; absence is reported as a skip. macOS-native window behavior
must be tested on macOS.
