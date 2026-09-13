# TikuReader

TikuDesktop's **Applications → TikuReader** opens an independent, offline
Markdown/HTML document browser. The application is shell-owned, like Device
Monitor; it reuses the help reader rather than duplicating the document engine.
File, Edit and View menus publish into the desktop's global menu bar.

The document window uses NetPositive/WebPositive-inspired native browser chrome:
compact, normally flat Back, Forward, Reload, Open and Find icon buttons with
labelled hover tips. The Location bar shares the toolbar row at normal widths,
and moves below the icons on narrow windows. Icons use the toolkit's cached
scalable HVIF artwork, not tiny bitmap glyphs inside large text buttons.
Keyboard focus, pressed states and disabled history controls remain visible;
automation still receives labelled button controls. Help keeps its controls. Copy tools
live in Edit; Outline and Bookmarks are optional, checked View menu items.

TikuReader's dedicated `reader` HVIF icon is an open book with blue covers
and a gold bookmark. Applications, Startup preferences and the reader's status
strip use the same cached vector artwork, authored in `tools/mkreadericon.py`.

The title-tab maximize button fills the usable desktop without covering its
menu bar or Deskbar. A second click restores the exact earlier frame. Manual
resizing establishes a new restore frame on the next maximize, and reflow keeps
the logical reading position. Pointer hover events also reach the built-in
Reader, including an outside move to clear tips after leaving its window.

Location / Ctrl+L selects the address. Enter or Go opens a literal local filename,
absolute or relative to the current document's folder; `#` and `%` in filenames
are not interpreted as URL escapes. Escape restores the displayed address.
The native field accepts 255 UTF-8 bytes and refuses oversized input atomically.
For longer absolute paths, Open still accepts the full supported path, the bar
displays it unfocused, and editing uses its basename within the current folder.
No web addresses are fetched. Failed opens preserve the document and history.

Find / Ctrl+F reveals a bottom search bar. Escape or its close button hides it
without closing the document. Enter / Shift+Enter and Ctrl+G / Ctrl+Shift+G move
between matches. Search folder remains available in Edit, including in narrow
windows where its search-bar button is hidden.

Open / Ctrl+O uses the native file picker. Reload / Ctrl+R rereads the saved
document. Back/Forward (also Alt+Left/Right), search, outline, text/code copying
and bookmarks reuse the existing reader tools. Closing the window keeps its
in-session state; starting a new desktop does not reopen a document automatically.
Startup preferences can launch the welcome window, disabled by default.

Explicit file opens accept regular local `.md`, `.markdown`, `.html` and `.htm`
files, case-insensitively. File-panel namespace paths are resolved through the
serving backend; only POSIX files are accepted, not device VFS endpoints.
Filenames passed to Open are literal. Links decode percent escapes and resolve
relative to the canonical displayed file's directory, including `../` and
fragments. Navigation may cross directories on an explicit click; automatic
images must remain beneath the displayed file's directory, including after
symlink resolution. Remote URLs, file URLs, scripts and executable files are
not launched. Ordinary documents do not use Help's language-directory fallback.

History stores canonical absolute filenames, and failed reads do not commit
history. Reload preserves logical position without adding an entry. Bookmarks
use `$XDG_CONFIG_HOME/TikuReaderBookmarks` (or `$HOME/.config/`) and are independent
of Help bundles. `TIKU_READER_BOOKMARKS` overrides the file; an empty value disables
persistence for tests. No document file is written. Clipboard, image and syntax
limits still apply; see [help-reader.md](help-reader.md).

## Large documents

The original 16 KiB source cutoff is removed from desktop file reading, including
Help. Files up to **8 MiB** are read completely into temporary heap storage.
Larger files, binary NUL-containing input, and failed reads are refused without
replacing the displayed document or its history. Generated application reports
retain their separate existing message-size limit.

The renderer has per-document text and table capacities, with 32-bit lengths,
indices and geometry. Desktop tables grow on demand up to a **64 MiB table-storage
budget per document**. Search/selection/outline buffers scale with the parsed
document, and folder search reads full sources with a 32 MiB aggregate budget.
Markdown conversion sizes its escaped HTML output rather than always allocating
a worst-case multiplier, and bounds delimiter-scanning work on malformed input.

The 64 MiB budget is not a total application-memory limit: source, conversion,
search, images, the old page and the candidate page can coexist temporarily.
Very narrow layouts, large fonts and unusually complex markup can still fill a
layout table; the renderer displays an explicit capacity notice. This is not an
unbounded streaming or paginated browser.

Embedded callers still use fixed default capacities without heap allocation in
the HTML parser. They may opt into caller-provided storage through
`tiku_doc_init_storage`; that memory remains caller-owned. The struct layout has
changed and must be rebuilt on both ends. Wider indices also increase the small
profile's table footprint; the capacity macros can still be reduced for a board.

First-version limits: one reader window, no tabs, editing, printing, web browsing,
or automatic file associations. Use Open to read a document without changing its
existing editor association. Folder search is explicit, bounded and synchronous.

Verification: `test_document_reader` exercises native window events, relative and
escaped links, history, reload/failure preservation, empty files, search, outline,
bookmark persistence, localization isolation, local images, symlink confinement,
and rejection of FIFOs, scripts and remote links. Existing Help tests cover the
unchanged bundled-help behavior. No connected devices are needed for these tests.
It also checks browser button hit-testing, Find visibility, Location keyboard
and Unicode input, clipboard routing, Go, address/history synchronization,
failed-address preservation, narrow layouts and long-address rejection/fallback.
It also checks vector rendering at several sizes, labelled icon controls,
hover clearing, maximize/restore geometry and reading-position preservation.
`test_doc_large` covers 120,000-byte runs, tall code blocks and over 32,767
blocks/runs/lines/pieces. `test_reader_large` covers megabyte-scale Markdown,
tail search and copying, reload, persisted late-page bookmarks and safe refusal.

The `documentreader.script` desktop lane covers menu launch, native File/Open,
cancellation, Ctrl+O, Markdown display, Find, Reload, contextual Help and closing
the application without shutting down the desktop.
