# Help reader and application reports

The document engine remains a bounded, offline HTML dialect, not a browser.
The reader also converts `.md` and `.markdown` files (case-insensitive suffixes)
through the interface kit's `tiku_markdown_html` API into that same document model.
The Help window now adds Find, cross-page search, selection/copying, an outline,
bookmarks, local images, link previews, contextual topics and generated reports.

## Reading controls

- Ctrl+F focuses Find; Enter / Shift+Enter and Ctrl+G / Ctrl+Shift+G walk matches.
  ASCII matching ignores case; other UTF-8 text matches exactly. Phrases may span
  inline elements. Cross-page search examines decoded words, not HTML tags.
- Drag selects text. Ctrl+A selects all page text when the page owns focus.
  Ctrl+C / Copy transfers the selection. Copy code copies the clicked code block,
  or the first visible code block. The existing 4096-byte clipboard rejects
  oversized text atomically. Copy never runs a command or uploads an example.
- Outline follows headings, while Bookmarks retains up to 32 logical positions.
  Both sidebars scroll independently and move above the document on narrow windows.
- Hover previews link destinations; Copy link copies the keyboard-selected link.
  Web links are not launched. Missing cross-page anchors are reported explicitly.
- Resize and font changes preserve a logical reading position. History stores
  both logical and pixel positions, using pixels only when the layout matches.

## Resources and limits

Page and image paths are relative to the help root, not the current page's
directory. Traversal is rejected, canonical paths must remain inside the help
root, and only regular local files are accepted. Script/style content is skipped.
No network fetches or automatic namespace actions are introduced.

Desktop documents use per-page heap-backed capacities; embedded callers retain
the fixed-capacity profile. A scan considers at most 128 HTML/Markdown files
and descends at most four directory levels, with a 32 MiB aggregate source budget,
reporting partial results at a limit.
Search is an explicit local scan; it does not read device VFS files. The sidebar
shows one matching result per page with an excerpt.

## Markdown

Supported: ATX and setext headings, paragraphs and hard line breaks, bold and
emphasis, inline code, backtick/tilde fenced code, inline links and images,
indented nested lists, quotes, horizontal rules, and simple pipe tables.
Code fences preserve whitespace and literal HTML/entity characters; their
optional language label does not enable syntax highlighting. Raw HTML is escaped
and displayed literally. Heading slugs are lowercase, retain letters/digits,
hyphens and underscores, and replace spaces with hyphens. Duplicates receive
`-1`, `-2`, etc. Duplicate-slug tracking retains the first 64 heading IDs;
the desktop renderer's anchor table itself can grow beyond 64.

This is a documented subset, not CommonMark/GFM conformance. Reference-style
links, indented code blocks, footnotes, task checkboxes, math, autolinks and
embedded HTML are not interpreted. Headings 4–6 use heading style 3, ordered
lists start at 1, tables ignore alignment colons, and pipes in table code spans
must be escaped. Separate list items with a single newline (blank lines end a
list). Existing help-root-relative link/image paths and confinement apply to
Markdown too, including localized pages. `.md` topics can be registered using
the existing `help_page`/`help_open` app services; generated reports remain HTML.

Desktop source reads accept complete files up to 8 MiB, with a 64 MiB per-page
table-storage budget; they no longer return a silently incomplete prefix.
Oversize, unreadable, binary or over-budget files leave the current page/history
untouched. Markdown conversion counts its exact escaped output before allocating
and has a delimiter-scanning work bound. There are no new dependencies,
filesystem access from the converter, or network operations. Generated reports
keep their separate existing API/message limits. See
[document-reader.md](document-reader.md) for memory and layout caveats.

The image cache holds eight images per page. Files must be at most 4 MiB, with
dimensions at most 2048 per axis and one million pixels before decoding. PNG,
uncompressed BMP and baseline JPEG use the existing translation-kit decoders.
Images scale down with aspect ratio retained; unsupported images use alt text.
The interface kit still does not depend on the image decoder.

Headings use the desktop's measured 10/12/14/16-pixel ladder. At its largest size,
or on a board with one baked metrics size, bold is the deliberate fallback.

## Localization and bookmarks

`TIKU_LANG` (or `LANG`) selects an optional language subdirectory: `fr_FR.UTF-8`
looks under `help/fr/`. Each missing page or image falls back to the base folder.
The French contents page is provided; untranslated topics remain in English.
The toolbar's new labels are in the English and French string catalogues.

Bookmarks are stored per help bundle in `$XDG_CONFIG_HOME/TikuHelpBookmarks-HASH`
or `$HOME/.config/TikuHelpBookmarks-HASH`. Writes use a temporary file and atomic
replacement. An unavailable preferences directory is reported and bookmarks
remain session-local. `TIKU_HELP_BOOKMARKS` overrides the file for tests.

## Application API

Rebuild applications with the current services header and check callbacks before
calling them. `help_page(ctx, "apps/example.html")` still registers menu help.
`help_open(ctx, "apps/example.html#setting")` opens a contextual topic following
a user action. F1 and the desktop Help menu also recognize shell-owned settings,
Device Monitor and VFS Inspector.

`help_html(ctx, title, body)` opens a generated information page. The title is
escaped by the host; applications must HTML-escape dynamic values in the body.
Bodies are limited to 15000 bytes and titles to 127 bytes at the services layer.
Both embedded apps and remote apps support these calls. Remote return values
indicate that the request was sent, not that the host has acknowledged rendering.

The reader keeps eight generated documents for Back/Forward navigation, without
writing them to disk. They are not bookmarkable. Closing the Help window retains
the bounded reader state; desktop teardown frees it. An evicted report behaves
like an unavailable page. TikuBench's View / Results report is the first consumer:
it escapes device text, reports truncation, and never runs tests or touches a board.

## Verification

`test_help_reader` covers search across markup, Unicode input/copy, image loading,
reports/history, clipboard overflow, bookmarks, locale fallback, confinement,
and logical positions across resize/font/history. Existing document/paint/window
tests remain applicable; the shell Help lane checks real desktop menu navigation.
