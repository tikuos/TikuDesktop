# Tests

Unit suites are grouped by the component they exercise: `system/`, `kits/`
and `apps/`. Cross-component tests and driven shell scripts live under
`integration/`. Data, stub headers, fault-injection modules and helper servers
live under `fixtures/`; the external `trk-conduct` driver source is in `tools/`.

`mk/tests.mk` discovers `test_*.c` directly under the four groups. Basenames
must be unique. The binaries retain their original names in `build/tests/`.
Application framework `test_app` has its own link rule in `mk/apps.mk` and
is included in the test run. Host-specific suites are selected by `TIKU_HOST`.

`test_remote_snapshot` verifies that queued application repaints cannot change
the desktop's displayed pixels or overwrite unpublished shared buffers. It
covers acknowledgment reuse, delayed/duplicate/malformed releases, resize and
window changes, token exhaustion, failed mapping/allocation, malformed publications,
older-peer compatibility and TCP fallback. `test_shm` covers shared-memory
reservation failure, serial fallback, switching between display lists and
pixels, and mapping cleanup. Both also run in the sanitizer regression group.

`make test-document-write` runs TikuWrite workflows and guarded atomic writes
in normal and sanitizer builds. `make test-document-output` checks native
paper properties, preview clipping and PDF geometry/text. These require an
installed font but do not open a desktop or a board. The separate-process
window test is `TRK_SHELL_ONLY=write.script sh tests/integration/shell/run.sh`.
The picture workflow runs with `TRK_SHELL_ONLY=writepictures.script` instead:
it opens an embedded-picture file from Tracker, selects and resizes the
picture, checks drawing against a full redraw, and undoes the size change.
The table workflow is `TRK_SHELL_ONLY=writetables.script`: Tracker launch,
cell editing, Tab, all six row/column menu commands, table size/padding,
border and column-width panels, equal widths, cell shading/clear, divider
dragging/cancellation, undo, preview, zoom,
maximize and full-redraw comparisons.
`TRK_SHELL_ONLY=writecontinuation.script` checks the header-row panel, undo,
page navigation and repeated header fill/image/border pixels in a long table.
`TRK_SHELL_ONLY=writecells.script` checks row/column/table selection, Alt-drag,
Shift+arrow and Ctrl+Shift+Home extension, shrinking and highlight pixels,
input refusal, whole-range fills, clearing, undo and highlight pixel/redraw
equivalence in the separate TikuWrite process.
`TRK_SHELL_ONLY=writecellscroll.script` resizes a long-table window, holds a
stationary captured pointer below/above the viewport, checks scrolling and
selection pixels in both directions, and releases it over the toolbar.
`TRK_SHELL_ONLY=writecellpaste.script` copies a multiline row, closes its
source process, reopens the document, refuses shape and edge mismatches,
pastes through Edit / Paste, saves, and checks rectangular Cut, undo/redo
and selection redraws.
`TRK_SHELL_ONLY=writecellinsert.script` copies a row, closes its source process,
inserts it as a new table in another process, and checks one-step undo/redo and
full-redraw equivalence. The model suite also checks insertion at Unicode
caret boundaries, retained formatting/anchors and each allocation refusal.
`TRK_SHELL_ONLY=writenestedpaste.script` transfers a nested table after closing
the source process, inserts another copy at a cell text caret, and checks
one-step undo/redo and redraw equivalence.

`make test-document-tables` checks native table hierarchy round trips,
malformed ownership, required-feature handling, preview and PDF borders/text
in normal and sanitizer builds. The headless document suites additionally
cover nested table insertion/deletion, cell boundaries, split/join and styled
paste, hit testing, narrow views, whole-row pagination, retained snapshots,
undo/redo and injected allocation failures. TikuWrite tests cover its table
panel, editing, protected Cut, save/reopen and undoable deletion.
`test_richdoc_grid` adds 2000 independent matrix comparisons for row/column
changes and undo/redo, nested-cell preservation/removal, stable identities,
caret remapping and bounds. The command fault suite injects allocation
failures into all six row/column commands. Write tests also publish 40 styles
to exercise the fixed menu capacity and style submenu.

`test_richdoc_table_props` checks relative-width validation, geometry edits,
atomic rollback, stable IDs, undo, weight changes on column insertion/deletion,
200 layout oracles and minimum widths around pictures and nested tables.
TABLE_SET participates in command allocation-failure tests. Native tests
round-trip explicit weights, reject malformed/undeclared column metadata and
compare border positions in preview and decoded PDF. TikuWrite tests check
settings in narrow windows and refusal after the document or target changes.

`test_richdoc_cell_drag` checks cell fills and 100 divider drag oracles across
zoom/scroll values, nested hits, content bounds, history and interrupted edits.
CELL_SET participates in allocation-failure tests. Native tests check required
fill metadata, exact save/reload and opaque/translucent preview/PDF pixels,
including borderless cells. The Write suite routes actual mouse events and
checks hover cursors, field validation, stale-cell refusal, clear/undo and reload.

`test_richdoc_pagination` checks 300 independent row/header packing cases,
incremental/full equivalence, header edits and grid changes, nested and
successive tables, oversized-line refusal, page limits, retained lifetime,
allocation failures and cancellation. `test_richcontinuation` runs under
`test-document-tables`: equal/weighted-column native round trips, malformed
header metadata, repeated styles/images/links, preview pixels and independently
decoded PDF pages. The Write suite checks strict whole-number fields, no-op
Apply, undo and reloaded header settings.

`test_richrowpages` adds 300 independent tall-row packing cases, unequal font
metrics, exact line coverage, eight nesting levels and repeated headers,
step-size equivalence, allocation refusal and cancellation. It runs in the
normal and sanitized Document lanes. `test_richrowoutput`, part of
`test-document-tables`, independently decodes PDFs to check body text exactly
once, image area, link bounds and preview/PDF fills and borders. It also checks
every observed export cancellation point. `writerows.script` navigates tall
nested rows in the separate TikuWrite process.

`test_richcells_navigation` checks 1152 directed keyboard combinations,
row/table boundaries, nested tables, read-only and busy state, zoom, elapsed
time clamps, backward clocks and drag cancellation. TikuWrite's app test
also drives stationary-pointer scrolling through its tick callback and
releases the captured pointer over the toolbar.

`test_richcells_paste` checks 60 fitting rectangles, literal quoting and line
ending cases, multiline defaults, old snapshots, undo/redo, tracked positions,
media replacement, nested-table refusal, exact operation and capacity limits,
command dispatch and allocation failures without partial edits.

`test_richcells` checks 144 cell-endpoint combinations against independent
matrix and geometry results; 4096-cell shading, clear-operation limits,
quoted tab-separated text, short-buffer safety, nested/media refusal, retained
snapshots, undo/redo and anchor restoration. View checks cover independent
selections, zoom/scroll, captured input, read-only state and command routing.
Allocation-failure checks preserve content, selection and exact accounting.
The Write suite checks menu selection, painting, clipboard failure, bulk edits
and stale shading panels.

`make test-document-media` checks embedded pictures in normal and sanitizer
builds: exact native-file round trips, malformed pixel data, memory limits,
alpha blending, PDF pixels and picture hyperlinks. It needs an installed font
but no desktop or board. The Write suite also covers worker insertion, width
and alternate-text edits, undo/redo, stale insertion refusal, safe clipboard
refusal and reopening a document after deleting the original picture file.

`make test-clipboard-typed` checks atomic rich/plain clipboard pairs,
allocation refusal, MIME/UTF-8 bounds, aliasing and request/reply round trips
in normal and sanitizer builds. `test_write` additionally covers rich
picture Cut/Paste, acknowledgment ordering, stale selection/content,
read-only changes, timeouts, malformed fragments and plain fallback.
`writerichclip.script` copies a picture, closes its source application,
opens a new process and checks pasted/cut/undo pixels.

`test_richcells_rich` adds rich rectangular paste with 60 placement oracles,
empty-cell defaults, conflicting styles, owned links/images, tracked positions,
self-paste, history, capacity/read-only guards and allocation failures.
`test_richcells_nested` validates parent links and unique identities independently
of the copier, including retained nested table settings, linked images,
combined-depth refusal and identity exhaustion.
`test_richcellclip` checks cell envelopes, exclusion of unselected content,
picture and empty-style ownership, malformed headers, truncations, budgets
and cancellation. The native-file fault suite covers each cell-envelope
allocation. These run under the document/model and document/files targets
in both normal and sanitizer builds. The Write suite checks acknowledged
rectangular Cut, rich Paste, changed rectangles and malformed rich replies.

`test_richsession_observer` covers directional selection events, affinity,
independent views, typing and external edits, undo/redo/import, suppressed
duplicates, allocation-free delivery and callback mutation guards. It runs
in both document targets; TikuWrite uses the observer to schedule repaint.

The headless Document Kit has isolated targets: `make test-document` and
`make test-document-sanitize`. They run model, style, editing/history,
split/join, tracked-position, multiline range, editable formatting/style,
incremental search, nested-list, hyperlink, mixed-selection, styled-fragment,
embedded-picture ownership, image selection and undoable corner resizing,
typed-command, per-view session, typing-group, publication-notification,
Unicode segmentation, layout, view navigation/reflow and allocation-failure suites
without opening a window or device. See the
[behavior contract](../docs/contracts/document.md) for scope and limits.

`test_richhistory` checks history's unique-allocation counts against an
independent allocator registry, including shared paragraph/link/image/table
storage, separate equal-content pixel buffers, styles, captured selections,
coalesced typing, oldest-group eviction and redo branching. It verifies a
one-byte edit in a 1 MiB document under a 96 KiB history budget, exact budget
boundaries, allocation-free Undo/Redo, held snapshots, failed-edit retries and
allocation-refusal cleanup. Both document targets include this suite.

`test_richlayout_cache` compares reused paragraphs with complete independent
reflow, including list renumbering, nested-table geometry, pictures, formatting,
undo, hit testing and carets. Its 1,000-paragraph fixture checks shaping-call
counts after a one-character edit; this is not a native-font timing benchmark.
Layout fault tests also cover retained-cache lifetime, cancellation, copied-line
limits and automatic cold retry under aggregate memory pressure.

`test_richoutline` checks semantic heading inheritance, explicit body overrides,
empty headings, headings in cells, incremental progress, neighboring positions,
undo/redo, retained source lifetime, allocation refusals and cancellation.
`test_richmarkup` checks native heading feature declarations and HTML/Markdown
meaning. TikuWrite tests exercise stale navigation, formatting, undo, sidebar
visibility, keyboard/pointer navigation and narrow-window layout.
`writeoutline.script` covers the actual menus, sidebar, save panel and redraws.

`test_richruler` checks point geometry independent of alignment, hanging/list
indents, inherited and mixed formatting, atomic multi-paragraph undo, cell
padding, cached layout measurements, zoom, stale captures, read-only state,
invalid inputs, the 1024-paragraph boundary and allocation-refusal cleanup.
TikuWrite's normal and sanitizer tests cover ruler dragging, cancellation,
keyboard changes, resizing, preview guards and narrow windows.
`writeruler.script` drives the actual ruler menu, pointer capture, keyboard
controls, undo and native save through the desktop.

`make test-document-performance` runs `test_richperformance` with an installed
TrueType face (`TIKU_TEST_FONT` can select it). It reports native-font reflow,
largest incremental call, viewport paint and model/layout allocation peaks
for 250, 1000 and 2000 paragraphs. Deterministic gates compare cached output
with fresh layout geometry/pixels, require substantial paragraph reuse and
verify undo and allocator cleanup. Timing has no hardware-specific threshold.
Model-only history workloads make 200 separate edits at each document size,
report mutation time, peak and additional-history bytes, and traverse all 200
Undo and Redo steps. Clearing history must release exactly the reported extra
storage when no independent snapshots remain.
`make test-document-performance-sanitize` runs the same workload under
ASan/UBSan. Neither target opens a display server or a board port.

`test_richlinkscan` checks adjacent styled link runs, separated occurrences,
directional endpoints, table cells, cancellation, allocation refusal and
snapshot lifetime through mutation and document release. `test_richview`
checks grapheme snapping; TikuWrite tests also cover read-only navigation,
cancelled scans and successive links within one multi-codepoint character.

`make test-document-media` checks shared native image payloads, per-occurrence
size/alternate text, content equality, reference chains and malformed-reference
refusals alongside preview/PDF output. The native fault fixture contains
repeated images. `writesharedimages.script` copies a picture twice, saves it,
closes the app and checks all three pictures after reopening the native file.

`make test-document-render` adds native glyph-ID and surface-drawing tests in
normal and sanitizer builds. It needs an installed DejaVu Sans or Arial font,
or `TIKU_TEST_FONT=/path/to/face.ttf`, but no window server or attached device.
The core document tests use deterministic providers and require no system font.

`make test-document-files` and `make test-document-files-sanitize` exercise
native `.tkrd` persistence, the stored ZIP writer and bounded package reader.
Editable markup tests (`test_richmarkup`) cover HTML entities, formatting,
whitespace, nested rectangular tables, headers, six heading levels, numbered
starts, unsupported-content reports, cancellation, memory limits and Markdown
conversion. The file fault suite walks each HTML/Markdown staging and model
allocation.
Embedded-picture tests compare exact PNG pixels, alpha and display metadata
after HTML export/import, including tall/wide images across DEFLATE blocks.
The fault suite refuses each PNG/staging/model allocation and measures scratch
peaks at 192 independent limits. PNG cancellation and malformed base64 clear
the document output. TikuWrite tests preserve the original file during import and
guard Markdown export against external changes. The `writemarkup.script`
desktop workflow opens an editable copy and exports through the real panels.

They also cover HTML export (`test_richhtml`): escaped text/links/font names,
resolved formatting, list numbering, nested tables, header groups, conversion
notices, output limits and cancellation in both passes. The normal file target
runs an independent Python HTML parser over the generated fixture. The native
fault suite refuses the HTML allocation and checks cleared outputs and ownership.
`TRK_SHELL_ONLY=writehtml.script` exercises File / Export HTML through the
desktop picker and checks that the native document remains unsaved.
`writehtmlpicture.script` exports a native picture document, checks its
embedded PNG URI and hyperlink, reopens the HTML and checks decoded pixels.
The imported copy requires an explicit save/discard decision on close.
`test_png_stream` runs 45 decoded pixel oracles and callback-refusal walks.
The Python HTML test independently checks embedded PNG CRCs, zlib RGBA
scanlines, transparency, display size, links and all three base64 padding cases.
They also run `test_richclip`: styled fragment envelopes, independent
style/link/picture expectations, transfer from an exited source process,
input-buffer lifetime, undo/redo, empty selections, hostile framing and XML,
table refusal, cancellation and byte budgets. `test_richio_faults` walks
fragment encoding and decoding allocations as well as native file paths.
They check exact formatting/text round trips, unsupported content, truncation,
CRC/header failures, cancellation, memory ceilings, injected allocation and
short-write failures, and malformed XML variations. They use private temporary
directories, not existing documents or connected devices. `test_richview_input`
in the core document target covers shared input, complete commits and deletion
rollback without a native window.

`make test-document-notes` runs the Sticky Notes embedding and clipboard
publication suites normally and with address/undefined-behavior sanitizers.
It uses isolated note directories and no device ports. Coverage includes
plain-file compatibility, wrapped selection, Unicode deletion, clipboard
refusal, undo, repeated resizing, queued input during reflow, snapshot saves,
failed-save retention and private emergency recovery. Notes uses the installed
font index, so this suite needs a readable installed TrueType face.

The `notes.script` desktop lane uses the real application runner to exercise
menus, double-click selection, clipboard, undo/redo, maximize/restore, closing
and saved files. Run it with `TRK_SHELL_ONLY=notes.script sh
tests/integration/shell/run.sh`; it does not access boards.

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

The read-only P4 storage-kit check uses its own `TRK_P4_PORT` variable:
`TRK_P4_PORT=/dev/serial/by-id/... ./build/tests/test_p4_live`. It opens only
that port, requires the native v1.3 P4 identity, and checks PSRAM, clock policy
and the paged GPIO manifest. Close any desktop/Bench session on the selected
port first. With the variable unset, it skips without opening hardware.
