# Device Monitor and VFS Inspector

Open these utilities from **Device Tools**. Both are native shell-owned windows:
they use TikuDesktop's mounted VFS backend, without opening another serial port
or extending the application IPC protocol. Developer Calculator is a separate
loadable app under **Development**.

Each device utility captures the desktop default when opened. **Use default**
explicitly adopts a new default; **Next device** cycles through connected boards.
Unplugging or changing the desktop default never silently switches the target.
All three utilities can be selected in Startup Preferences; none is enabled by
default. No utility scans ports, flashes firmware or restarts a board on startup.

## Device Monitor

The monitor samples after opening, then waits five seconds after each completed
sample before another request. Pause retains the last displayed snapshot; Resume
or Refresh resumes acquisition. Acquisition is serialized with other work on
the same mount and runs off the UI thread. Close stops further polling.
Closing cancels remaining utility reads without cancelling other apps' work.
Desktop shutdown waits up to five seconds for these jobs before releasing
their device backends.

- `/sys/uptime`: seconds since boot.
- `/sys/cpu/freq`: current clock, displayed in MHz, not the saved clock target.
- `/sys/mem/used`: process SRAM allocation, **not total SRAM consumption**.
- `/sys/mem/stack_free`: live stack headroom.
- `/sys/mem/sram`: SRAM capacity.
- `/proc/count` and up to eight numeric process directories: name, state, SRAM
  allocation and pending events. Pending events are not CPU usage or lifetime
  delivered-event counts.
- `/sys/mem/nvm_map`: Store capacity, allocated usage and free space. Used space
  includes filesystem metadata, slot slack and in-progress allocations. Older
  firmware reporting only capacity does not provide an occupancy reading.

Process RAM and Store allocation have bounded 120-sample histories (roughly ten
minutes at the nominal interval). Missing data and long gaps break the graph.
Sample age is displayed; unsupported or failed readings say Unavailable.
The monitor does not fabricate utilization percentages or sample sensor devices.

## VFS Inspector

On launch, the registry-style browser captures the default device and loads its
directory tree and known-safe values in the background. It initially selects
`/sys` (or `/` if unavailable), so useful system values are visible immediately
after loading. **Use default** and **Next device** also acquire a fresh snapshot.
The left pane retains an expandable,
alphabetically sorted directory tree; the right pane lists the selected
directory's values in **Name / Type / Access / Data** columns. Expand or collapse
branches with their +/− controls or the Left/Right arrow keys. Both panes scroll
independently. Enter an absolute device path and choose **Go to path** to jump
directly; **Up** returns to its parent.

The automatic snapshot reads advertised, readable endpoints for device identity,
uptime/time, boot counters, CPU clock settings, memory/storage accounting,
scheduler idle, crypto counters/mode and process name/state/RAM/events. Unknown
endpoints, hardware/device values, event drains, commands and user file contents
are listed but are not read automatically. Read permission alone does not prove
an endpoint is safe to query. Failed values say **Unavailable**, never zero.

Selecting a cached directory immediately shows its snapshot without serial I/O.
Uncached directories fetch metadata and their known-safe values. Click a value,
or select it with the keyboard and press Enter, to read that endpoint without
losing its siblings or the expanded hierarchy. Excluded endpoints say **Manual
read**; safe values not acquired within the scan limits say **Not read**;
displayed data is a cached snapshot, not a live subscription. **Refresh** reloads
the tree and safe values. Clicking an endpoint always reads its complete current
value before enabling editing; automatic previews never authorize a write.
Access labels reflect
device permissions, while Type identifies endpoints rather than inventing
Windows registry data types.

The value preview is bounded. **Copy value** copies the complete accepted text
even when only its first lines fit in the preview. Values larger than 2046 bytes,
embedded NUL or non-text data are refused. Directory snapshots contain up to
128 entries, and the tree caches up to 128 branches. Automatic acquisition stops
after at most 128 directory listings, 256 value reads, or 30 seconds (plus any
in-flight backend call), and reports a partial snapshot when limited. Closing
the window cancels remaining acquisition. Additional entries remain
reachable by their exact path. Selecting another device or detecting a disconnect
clears the old hierarchy and values. The tree is not persisted between windows.
Traversal components and control characters in paths are rejected.

Editing is deliberately narrower than the firmware's writable flag. Audited
settings are device name (up to 31 letters/digits/hyphens/underscores), clock
target, crypto mode (`auto` or `sw`), radio power in dBm and Unix epoch time.
Other writable nodes, including command endpoints, remain inspection-only.

1. Read a setting and edit its proposed value.
2. Review the target, path, current value and proposed value.
3. Confirm save explicitly, or cancel without writing.

The worker rechecks permissions and validation, compares against the value read
before review, writes once, and reads back. Clock targets must appear in the
device's advertised rates and require live/reboot mode. No reboot is automatic.
Wall-clock comparisons allow up to five minutes of normal advancement while
editing, and five seconds of advancement during readback. A lost response or
readback failure is reported as uncertain; writes are never automatically retried.
Refresh before retrying an uncertain write.

The separate value editor retains these safeguards; registry-style browsing
does not enable arbitrary command writes, deletion or Windows registry imports.

Tests use fake backends only: telemetry/missing values, path validation,
permission changes, stale writes, readback mismatch, explicit confirmation,
cancellation and controller lifetime after close. Registry tests cover automatic
discovery, safe-read filtering, permission filtering, unavailable values, cached
navigation without further I/O, partial scans, cancellation, bounded acquisition,
and tree
expansion, sorting, collapse, refresh pruning, preserved sibling values, keyboard
and pointer navigation, cache limits and disconnect clearing. Physical-device
verification
is separate and no firmware changes are required by these utilities.
