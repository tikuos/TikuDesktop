# Application failures and reports

Reports are saved automatically in `~/.config/tracker/reports`, or the directory
selected by `TIKU_TRK_REPORTS`. They appear alongside desktop reports in
**About this system → Last report**. Open the full report for all details.

- `crash-...-<application>-<pid>.txt`: a fatal signal, failed startup/module load,
  or unexpected desktop-connection loss. Fatal reports include the signal,
  fault address, instruction address, runtime build date, recent window/input
  events (including resize dimensions), and a best-effort stack trace.
- `exit-...-<pid>.txt`: the launcher's independent record of a failed child.
  This catches nonzero exit codes, missing executables and uncatchable signals
  such as SIGKILL even if the child could not write a report. It references
  matching child reports; it does not pretend the parent's stack is the child's.
- `latest-crash.txt` and `latest-application-exit.txt` identify the latest reports
  of those kinds. Separate timestamped files retain previous incidents.

Native standalone applications use the same reporter as applications launched
through `tiku-run`. The runtime records event types and window sizes, not typed
keys, document text or clipboard contents. Reports can contain local binary
paths, application-authored trail messages and identifiers: review before sharing.
New report files are owner-readable/writable (0600); they are not uploaded.
This does not redirect arbitrary application stderr into a separate log.

The application watchdog also records callbacks that stop making progress for
five seconds, in `stall-...` reports and `latest-stall.txt`. It reports without
killing the app. A legacy `trusted/` installation now has its own process too,
so its stalled callback no longer stops the desktop's event loop.

The runner is `build/system/tiku-run`; application modules and private helpers
are under `build/apps/<id>/`. See [architecture](ARCHITECTURE.md).

Normal window closes, successful exits, deliberate desktop shutdown, and a
launcher-tracked Force Quit are not recorded as crashes. An abrupt desktop loss
is reported as a connection failure, not mislabeled as an application signal.
The shutdown notice is best effort; a blocked channel or an older desktop may
still produce a connection-loss report when closing.

The launcher can only observe its own children. SIGKILL cannot supply a child
stack; an OOM kill is a possible cause, not something the signal alone proves.
Basic fatal-signal details and the event trail are written before attempting
stack unwinding, so a broken unwinder need not erase the useful evidence.
Keep matching binaries when investigating offsets with `addr2line`/a debugger.

## Resize failure fixed

The remote transport previously accepted only 1024 pixels per dimension, while
applications could resize further. The Editor reproduced a disappearing window
at 1280 × 800: its frame caused the desktop to disconnect it, and the client
returned success without a crash report. The shared transport now accepts up to
8192 pixels per dimension, with validation on both ends. The new real-process
Editor test exercises resizing from 480 × 260 through 3840 × 2160.

Linux shared surfaces now reserve their backing storage before writes. If tmpfs
cannot reserve it, rendering falls back to copied frames instead of risking a
SIGBUS when writing an apparently valid sparse mapping. Available memory still
limits practical window sizes. Restart the desktop and applications together
after rebuilding so both ends use the updated transport.

## Regression coverage

- `test_editor_resize`: actual Editor process and desktop transport; grow,
  shrink, and close normally.
- `test_client_reports`: fatal resize callback, startup failure, disconnect,
  ordinary exit and deliberate desktop shutdown.
- `test_launch_reports`: 24 children with one-slot result batches, nonzero exit,
  SIGKILL, intentional Force Quit and immediate exec failure.
- Existing signal/stack-overflow, transport, shared-surface and wakeup tests.
