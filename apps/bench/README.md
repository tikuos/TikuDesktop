# Bench in TikuDesktop

Open **Device Tools > TikuBench** after building `make apps`. This is a native
TikuDesktop-kit frontend, not a GTK window embedded inside the desktop. It has
the GTK GUI's shape at the desktop's size: a column of cards on the left
(Device, Kernel history, Connection, Build mode, Options, ePaper kit, Make,
then Run selected and Stop) that stay in view, and on the right the pills
counting passed / failed / did not run, a progress bar and the tabbed views
(Suites, Results, Output, Console, History, Benchmarks, Did not run). The
window is resizable from its corner and by its zoom box: the views grow, the
cards keep their column, and 960x620 is the least it will be, which fits
under the global bar on the 1000x680 desk with its tab in reach.
It reads `TikuBench/gui/tb_meta.py` and runs the existing
`python3 -u -m tikubench run ...` backend. No test/build/flash implementation is
copied into the desktop. Set `TIKU_BENCH` to another checkout if needed.

## Running tests

1. Connect a device through TikuDesktop. Bench initially uses the desktop's
   default device; its Device dropdown can explicitly select another mounted
   board. A disconnected selection never silently switches to another board.
2. Filter by suite, group, test ID or label. Expand suites with the disclosure
   arrow or Enter; select with a checkbox or Space. Whole-suite selection remains
   a full run; individual selections become one comma-separated `--only` argument.
   Different selected suites run in registry order, one after another.
3. Review the cards. **Monitor existing firmware** is the startup default and
   passes `--skip-build`. **Build + flash, then monitor** must be explicitly
   chosen and passes `--flash`.
   Test execution itself can still reset hardware or write storage.
4. Choose **Run selected**, review the target/mode/queue confirmation, then
   choose Run tests. The default button is Cancel. Tab switches the confirmation
   button before Return; a pointer action requires a press and release.

A destructive sub-test (drawn in red, never in a full sweep) is asked about on
its own before the run's question, Cancel first, and `--allow-destructive` is
given for that run only. All (Select visible in the menu) selects the displayed suites in full, not just
matching children. None removes all selections, including filtered ones. Profile changes clear
selection. Unsupported tests remain visible but disabled with an exclusion
reason below the list. Dedicated/diagnostic leaves are labelled and are never
implicitly selected as part of a full kernel sweep. Eligibility, full-profile
ownership and exclusions come from the same metadata as the GTK GUI.

Options include UART baud, MSP430 large memory model, RP2350 USB console,
CRACEN public-key hardware, ePaper kit enablement, make variables and extra C
flags. Make variables are whitespace-separated `KEY=VALUE` tokens, without shell
quoting. Use the memory-model checkbox instead of a conflicting MEMORY_MODEL
token. Extra C flags are passed as a single argument. USB uses the runner's port
discovery and omits explicit UART port/baud, as the GTK frontend does.

## Output and evidence

- **Results:** planned rows; the pills and progress bar over the tabs carry
  the pass/fail/skip counts and how far the queue has got. Zero assertions
  are a skip, not a pass. Missing completion markers and nonzero/signal exits
  fail the run. Host-side `harness_fail` revisions override firmware-green rows;
  kernel firmware totals remain provisional until host validation arrives.
- **Output:** test output with build/flash blocks and control records removed.
- **Console:** runner output and invocation arguments. Both text views retain
  the latest 256 lines; Follow tail resumes scrolling after a wheel gesture.
  Copy visible log copies that retained view, not the entire file.
- **Input:** send a line to an interactive suite. It is delivered asynchronously,
  with partial writes queued; a closed input channel cannot crash the app.
  Human unplug/replug requests are shown as an inline action notice.
- **History:** active-profile full, selected and diagnostic kernel records stay
  separate. A selected pass never replaces a failed full result. The display's
  explicit stale indicator uses a 24-hour age threshold, not release qualification.
- **Benchmarks:** active-profile baseline/current comparisons, including sample
  counts and the backend's regression/suspect classification. These are not
  functional-test verdicts.

Each queue gets a unique mode-0600 log in `/tmp/tikubench-desktop-*`. Open full
log opens it through the desktop where supported, otherwise copies its path.
Copy logs you want to retain: the OS can clean `/tmp`. Clearing the view never
deletes the run log or the backend's own persistent results ledger.

## Device handoff and stopping

Bench captures the device port and MCU **before** requesting release, freezes
the selected target/options, and waits for the matching acknowledgement before
launching. It checks that the device has not changed during confirmation.
The port stays released for the complete queue. Stop sends SIGINT to the owned
process group, escalates to SIGKILL after two seconds if necessary, and reaps
the runner before reclaiming. Cancelling during release never starts a test.
If reconnecting fails, Bench says to reconnect through Devices rather than
claiming that the board is connected again. Reflashed firmware must provide a
compatible desktop console to reconnect successfully.

Metadata and run output are read asynchronously with per-tick bounds. Registry
overflow/malformed required records disable Run; oversized output marks the
evidence incomplete. The current catalogue imports 95 suites / 502 sub-tests;
those numbers are not hard-coded.

## Parity boundaries

The suite list is grouped under the catalogue's category headings, a Did not
run tab lists what the profile leaves out with the reason, the Kernel history
card carries the verdict (UNPROVEN / STALE / fresh), and the ePaper panel and
pattern presets are the GTK GUI's own tables. The options, the device and the presets
are kept in `~/.config/tikubench-desktop/settings` (`TIKU_BENCH_SETTINGS`
names another file), written when one changes and read at start, under the
GTK GUI's key names. Bench uses devices connected through TikuDesktop instead
of independently scanning and opening arbitrary serial ports.

## Verification

`build/tests/test_bench` runs against a synthetic Python metadata/runner fixture:
no hardware imports, device I/O, builds or flashing. It covers profile selection,
queue/argv construction, authoritative corrections, no-test results, final
unterminated lines, nonzero exits, stdin, forced cancellation and port handoff.
It also reads the actual TikuBench catalogue and renders screenshots to
`/tmp/tiku-bench-catalogue.png`, `/tmp/tiku-bench-options.png` and
`/tmp/tiku-bench-output.png`, and
round-trips the settings file through a directory of its own.

The optional `bench*.script` hardware smoke tests use the new filter/selection
workflow and inline Results; they require explicit `TRK_PORT` setup. They are
not part of the fake-device safety tests and were not run during this port.
