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

`document/tiku_document_model.c` is a headless Document Kit example: it loads
styled paragraphs, changes and formats a reading in one transaction, and
undoes it. It also pastes a paragraph and checks tracked-caret restoration.
It edits a named style and paragraph alignment, then searches for a reading
and replaces it with a generation-checked edit.
It adds a numbered list item and a hyperlink, inspects mixed selection
formatting, then copies styled content into a second document without opening
either a window or a network connection.
It also queries a toolbar's mixed Bold state and applies/undoes that command
through the shared command API.
It creates two editing sessions with independent typing attributes and
observes the text edit and undo through document-change callbacks.
Build with `make build/examples/tiku_document_model` and run that
binary directly. See the [Document Kit guide](../kits/document/README.md).

`document/tiku_document_layout.c` renders styled paragraphs and numbered lists
with Document Kit geometry and Translation Kit's native fonts, without a
window. Build `make build/examples/tiku_document_layout`, then run
`./build/examples/tiku_document_layout build/examples/document-layout.png`.
The output path is explicit; existing files there are replaced.

`document/tiku_document_file.c` writes a styled report as `.tkrd`, marks the
saved snapshot and reopens the package through the checked public importer.
Build `make build/examples/tiku_document_file`, then run
`./build/examples/tiku_document_file build/examples/sensor-report.tkrd`.
It replaces the explicitly named file; it does not change file associations
or open a desktop window. The package can also be inspected with a ZIP reader.
