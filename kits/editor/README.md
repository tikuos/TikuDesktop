# Editor kit

The editor kit provides a text document and a view that displays and edits
it. Editor, TikuBasic, and TikuC use the same kit. It is a native C library;
building it does not require Scintilla, Lexilla, or a C++ runtime. The
document engine includes adapted Scintilla algorithms, recorded in
[UPSTREAM.md](UPSTREAM.md).

## Choose the parts you need

| Application | Configuration |
| --- | --- |
| Plain text editor | Document and view; no language; optional wrapping |
| Source editor | Add line numbers, language colouring, brackets, and folding |
| Read-only text viewer | Mark the document read-only; retain selection, search, and copy |
| Split editor | Create two views of one document; each has its own selection and scroll position |

A document owns text, undo history, anchors, and shared annotations. A view
owns its selection, layout, scrolling, and editing options. The application
owns windows, files, encoding conversion, menus, project state, and device
connections. Do not put a BASIC runner or a file picker into the kit to
reuse its editor.

## A minimal embedding

This example owns one reference to the document. The view holds another.
Call `page_open()` on a new `page_t`, and `page_close()` when finished.
The host passed to `page_open()` supplies clipboard and invalidation
callbacks; pass NULL for an editor without those services.

```c
#include <string.h>
#include "tiku_codeview.h"

typedef struct {
    tiku_editdoc_t *doc;
    tiku_codeview_t *view;
} page_t;

void page_close(page_t *page)
{
    tiku_codeview_destroy(page->view);
    tiku_editdoc_release(page->doc);
    memset(page, 0, sizeof *page);
}

tiku_edit_status_t page_open(page_t *page, const char *text, size_t length,
                             const tiku_codeview_host_t *host,
                             int source_editor)
{
    tiku_edit_status_t status;

    memset(page, 0, sizeof *page);
    status = tiku_editdoc_create(NULL, &page->doc);
    if (status == TIKU_EDIT_OK)
        status = tiku_editdoc_load(page->doc, text, length);
    if (status == TIKU_EDIT_OK)
        status = tiku_codeview_create(page->doc, &page->view);
    if (status != TIKU_EDIT_OK) {
        page_close(page);
        return status;
    }
    tiku_codeview_set_host(page->view, host);
    tiku_codeview_set_options(page->view,
        TIKU_CODEVIEW_VSCROLL | TIKU_CODEVIEW_HSCROLL |
        (source_editor ? TIKU_CODEVIEW_LINE_NUMBERS | TIKU_CODEVIEW_BRACES |
                         TIKU_CODEVIEW_FOLDS : TIKU_CODEVIEW_WRAP));
    tiku_codeview_set_language(page->view,
        source_editor ? TIKU_SYNTAX_C : TIKU_SYNTAX_NONE);
    return TIKU_EDIT_OK;
}

void page_paint(page_t *page, tiku_surface_t *surface, tiku_rect_t content)
{
    tiku_codeview_set_bounds(page->view, content);
    tiku_codeview_paint(page->view, surface);
}

int page_event(page_t *page, const tiku_event_t *event,
                tiku_edit_status_t *status)
{
    int handled = tiku_codeview_event(page->view, event);
    if (status != NULL)
        *status = tiku_codeview_status(page->view);
    return handled;
}
```

Route events only to the appropriate view. When `page_event()` returns
nonzero, request a repaint and update menu/status state. Check the returned
status before presenting an operation as successful. A document edit in
another view invokes the host's `invalidate` callback; schedule a repaint
there rather than editing the document during its notification.

Use `tiku_codeview_changes()` after an event or on a UI tick to find out
whether the document, selections, scroll position, command availability,
or last command result needs a status-bar or menu update. The first call
returns `TIKU_CODEVIEW_CHANGE_ALL`. Later calls consume the updates since
the previous call; several updates produce one set of flags. Each view
has its own polling state, so poll both panes of a split editor.

This is not a paint-damage API. After changing options, colours, folds, or
decorations, request a repaint yourself. Do not destroy the view inside a
host callback while its event or painting call is still running.

Use `tiku_codeview_command_enabled(view, TIKU_CODEVIEW_CMD_CUT)` to decide
whether to enable Cut. It checks read-only state, clipboard support, and
all selections, including secondary carets. It does not call the clipboard
provider or promise that a command will change text: Delete at the end of
a document, for example, is permitted but has nothing to delete.

## Add a language highlighter

BASIC and C remain built in. For JSON, install the supplied provider:

```c
tiku_edit_status_t page_use_json(page_t *page)
{
    tiku_codeview_set_language(page->view, TIKU_SYNTAX_NONE);
    return tiku_codeview_set_lexer(page->view, tiku_codeview_json_lexer());
}
```

Editor does this automatically for `.json` files. The JSON provider colours
strings, numbers, and literal keywords; it does not validate JSON. Editor
also calls `tiku_codeview_json_folds()` after changes to provide folding
for multiline objects and arrays.

To support another format, implement the `line` callback in
`tiku_codeview_lexer_t`. The callback receives a complete UTF-8 line without
its line break, and emits runs consisting of an ink and a byte count.
Runs must cover the entire line, use positive counts, use a built-in ink or
a style ID you have defined, and end at UTF-8 character boundaries. The kit
checks these rules before using the output. Invalid output or allocation
failure leaves the line plain; `tiku_codeview_lexer_status()` reports the
reason.

Set `stateful` when a token can continue onto another line, such as a
multiline comment. The kit starts line zero with `initial_state`, then
passes each line's outgoing 32-bit state to the next line. It may call the
provider with `emit == NULL` to calculate state without collecting colours.
After an edit, it recalculates affected states before displaying later
lines. With `stateful == 0`, each line starts with `initial_state`.

The kit copies the descriptor, not the object addressed by `ctx`. Keep
that object alive until the provider is replaced or the view is destroyed.
After changing its contents, call `tiku_codeview_lexer_changed()` and
request a repaint. Passing NULL to `tiku_codeview_set_lexer()` restores
built-in highlighting. Custom highlighting does not replace indentation,
comment or bracket rules; configure those separately with
`tiku_codeview_set_rules()` and `tiku_codeview_set_language()`. Folding can
use those rules or a table supplied by your language parser, as below.

Providers run synchronously on the UI thread. Do not retain their input,
edit the document, reenter the view, or destroy it from a provider callback.
Unlike the built-in long-line optimisation, a provider receives the entire
line, even when only part is visible. Expensive providers can therefore
delay the UI; background parsing is not part of this API.

## Define styles and fold regions

Built-in ink IDs take their colours from the desktop theme. You can
override a colour or define more styles, up to 64 IDs in total. Define an
ID before a lexer emits it. The `role` supplies a theme fallback and tells
bracket matching and function tips whether the token is code, a string, or
a comment.

```c
tiku_edit_status_t page_style_type(page_t *page)
{
    const tiku_codeview_style_t type = {
        .role = TIKU_INK_PLAIN,
        .flags = TIKU_CODEVIEW_BOLD,
        .colors = TIKU_CODEVIEW_STYLE_FORE,
        .foreground = TIKU_RGB(35, 75, 130)
    };
    return tiku_codeview_set_style(page->view,
                                   TIKU_CODEVIEW_STYLE_FIRST, &type);
}
```

After this call the lexer can emit `(tiku_ink_t)TIKU_CODEVIEW_STYLE_FIRST`
for a type name. Styles also support italic, underline, hotspots and an explicit
background colour. All use the view's font size and cell metrics; this is
not rich-text layout with a different font for each token. Selection and
diagnostic indicators remain visible over token backgrounds. Passing NULL
to `set_style` restores a built-in ID or undefines a custom one.

A parser supplies one `uint32_t` fold level for every document line. The
low 16 bits are nesting depth. Add `TIKU_CODEVIEW_FOLD_HEADER` to a line
that starts a region; its following line must have greater depth. The
region ends before the next line at the header's depth or a lower depth.
For three lines `begin`, `body`, `end`, the levels are
`{TIKU_CODEVIEW_FOLD_HEADER, 1, 0}`. Folding hides only `body`.

Pass the table, line count and the parsed document generation to
`tiku_codeview_set_fold_levels()`. It copies the table and refuses stale
results or invalid levels without changing the current folds.

An edit makes the table stale. Collapsed regions the edit does not touch
stay collapsed and can still be expanded, but nothing can be collapsed
until you supply levels for the new generation. When a new table arrives,
each collapsed region whose header line still starts a region stays
collapsed, over the lines the new table gives it, and the others expand.
The view keeps its scroll position, including a position inside a wrapped
line. Passing NULL and a count of zero restores rule-based folding the
same way. Changing the language or the lexer removes an installed table,
so configure both before installing one; without a table those changes
leave rule-based folds as they are.

For background parsing, make a document snapshot on the UI thread, parse
that owned copy in a worker, then install the resulting table on the UI
thread with the snapshot's generation. The kit does not start a worker or
infer fold regions from colour IDs.

## Search with regular expressions

Literal search remains the default. Add `TIKU_EDIT_REGEX` to a query for
POSIX extended regular expressions. For example, this changes `sensor=42`
to `42:sensor`, with all replacements in one undo step:

```c
tiku_edit_status_t page_swap_values(page_t *page)
{
    const char *pattern = "([a-z]+)=([0-9]+)";
    const tiku_edit_query_t query = {
        pattern, strlen(pattern), TIKU_EDIT_REGEX | TIKU_EDIT_MATCH_CASE
    };
    tiku_codeview_found_t result;
    return tiku_codeview_replace_all(page->view, &query, "$2:$1", 5, &result);
}
```

Use `$0` for the entire match, `$1` through `$9` for groups, and `${10}`
through `${31}` for higher group numbers. An unmatched optional group
inserts nothing; a group absent from the pattern is an error. `$$` inserts
a dollar sign. Replacement escapes `\n`, `\r`, `\t` and `\\` insert a
newline, carriage return, tab and backslash. Literal replacement mode does
not interpret any of these sequences.

Matches stay within one line; replacement text may contain line breaks.
`^` and `$` match each line's start and end. Repeated Find advances past a
zero-width match, and Replace All scans the original text once. Replace
treats an empty selection as a match only when Find selected it as a
zero-width match; a caret placed anywhere else is never replaced. A malformed
pattern returns `TIKU_EDIT_BAD_PATTERN`; an invalid replacement template
returns `TIKU_EDIT_BAD_REPLACEMENT`. Replace All validates and prepares all
changes before editing, so a failure leaves text and undo history intact.

This uses the host's POSIX regex library, not PCRE: use `[0-9]`, not `\d`;
lookaround and lazy quantifiers are not supported. Regex matching uses a
UTF-8 locale of the kit's own without changing the process locale:
`C.UTF-8`, or `en_US.UTF-8` on a host without it, where a bracket range
such as `[a-z]` may follow that locale's order of letters. A host with
neither locale returns `TIKU_EDIT_INVALID`. Case-insensitive regex matching
follows the locale; literal matching folds ASCII letters only. Searches
run synchronously and have no cancellation or execution-time budget, so
expensive patterns or large documents can delay the UI.

## Offer completion and function tips

The simple string-list API remains available. Use
`tiku_codeview_complete()` when a list needs separate display labels,
insertion text, descriptions, or a kind badge:

```c
tiku_edit_status_t page_complete(page_t *page, tiku_edit_range_t word)
{
    const tiku_codeview_completion_item_t items[] = {
        {"Print", "PRINT", "Write a value", TIKU_COMPLETION_FUNCTION},
        {"PrintLine", "PRINT\n", "Write a line", TIKU_COMPLETION_FUNCTION}
    };
    const tiku_codeview_completion_t list = {
        .range = word, .items = items, .count = 2, .prefix = "pr",
        .flags = TIKU_COMPLETION_SORT | TIKU_COMPLETION_IGNORE_CASE,
        .rows = 8, .current = SIZE_MAX
    };
    return tiku_codeview_complete(page->view, &list);
}
```

The kit copies the entries and filters their labels by prefix; sorting and
case-insensitive comparison are optional, with case folding limited to
ASCII. `current` is an index in the supplied array, not the filtered list.
Use `SIZE_MAX` to select the first result. Up and Down choose an item;
Return, Tab or a click accepts it. `accept` can name extra printable ASCII
accept characters: these trigger insertion but are not inserted themselves.
`tiku_codeview_completion_result()` reports the original input index once
after acceptance. A host can resubmit the list after typing to refresh it.
An empty result hides the old list; a failed update preserves it.

For a function tip, pass the document position of its opening `(`, a
signature string, and byte ranges describing the parameters to
`tiku_codeview_signature_show()`. For `f(first, second)`, the ranges are
`{2, 7}` and `{9, 15}`. The kit highlights the active parameter as the caret
moves. Commas inside nested brackets, quoted strings and lexer-recognised
comments do not advance the parameter. The tip closes when the call ends
or the caret leaves its bounded scan range. The host chooses the function
and supplies its signature; the kit does not resolve symbols or run a
language server. Inserted text may span lines, but snippet placeholders,
overload selection and fuzzy completion are not implemented.

TikuBasic uses the richer list for keyword completion. Keyword Help inside
a recognised built-in function call opens an argument-aware tip; elsewhere
it retains the keyword help list.

## Add visual aids and zoom

Enable `TIKU_CODEVIEW_INDENT_GUIDES` in the view's options for dotted guides
in leading whitespace. Guide spacing follows the configured indentation
width, or tab width when indentation uses tabs. Call
`tiku_codeview_set_ruler(view, 80)` for an 80-column ruler, or pass zero to
hide it. Columns are measured in spaces of the view's font, with tabs
expanded at its tab stops.

`tiku_codeview_set_zoom(view, steps)` accepts -2 through +4. Each step
changes the built-in monospace size by two pixels relative to the desktop
default, without changing desktop font preferences. Zero restores the
default. Zoom is independent for each split pane and recalculates wrapping
and hit testing. A custom font disables nonzero zoom; setting a custom
font resets zoom to zero. Guides, ruler and zoom are view state, not saved
preferences.

Editor exposes these under View, with Ctrl/Command+=, - and 0 for zoom.
Zoomed text is drawn in monospace faces with display-list IDs 64 through
95, which a desktop built without them cannot draw: rebuild the
application and the desktop together.

## Override shortcuts and dispatch commands

A view accepts a copied table of shortcut overrides. Unlisted keys keep
their existing behaviour. This example assigns Command/Ctrl+J to Select
All and leaves Command/Ctrl+Z for the application to handle:

```c
tiku_edit_status_t page_bind_keys(page_t *page)
{
    const tiku_codeview_binding_t bindings[] = {
        {.key = 'j', .modifiers = TIKU_MOD_CMD,
         .command = TIKU_CODEVIEW_CMD_SELECT_ALL},
        {.key = 'z', .modifiers = TIKU_MOD_CMD,
         .command = TIKU_CODEVIEW_CMD_PASS}
    };
    return tiku_codeview_set_bindings(page->view, bindings,
                                      sizeof bindings / sizeof bindings[0]);
}
```

`PASS` makes the view return zero for that key; `IGNORE` consumes it without
performing an operation. Matching ignores ASCII letter case and Caps Lock,
but otherwise requires the exact modifiers. Duplicate bindings and failed
allocations leave the previous table installed. Pass NULL and zero to
restore defaults. Completion lists handle their keys first, and committed
`TEXT_INPUT` events are never interpreted as shortcuts.

A menu can execute the same named command with `tiku_codeview_execute()`.
Pass zero for its last two arguments except for `TIKU_CODEVIEW_CMD_MOVE`,
which takes a movement and an extend-selection flag. Check the returned
status; it is also available through `tiku_codeview_status()`.
The application still owns shortcuts for files, projects, and devices.
These APIs do not add a shortcut-preferences dialog or save bindings.

## Text cleanup and match lists

Use `tiku_codeview_transform(view, operation)` for trailing-whitespace
cleanup, leading-indentation conversion, ASCII case conversion, and line
sorting. Each change is one undo step. A failure leaves the document and
its selections unchanged; `TIKU_EDIT_NOTHING` means no change was needed.

Case conversion acts on the main selection and leaves non-ASCII bytes
unchanged. Other operations act on the full lines touched by that selection,
or the whole document when the main selection is empty. They preserve line
separators and whether the file ends with a newline. Indentation conversion
uses the view's tab width and does not change tabs inside text. Sort compares
UTF-8 bytes, case-sensitively; it is not locale-aware alphabetical sorting.
The edited range becomes one replacement, so anchors within it move under
the document's replacement rules. Undo restores the original selections.

Use `tiku_edit_collect()` to obtain a bounded list of literal or regex
matches without editing the text or selecting it. Supply a byte range and
a caller-owned output array. Check both the returned status and `truncated`:
the array is incomplete when another match exists beyond its capacity.
On error, `count` is zero and partial array contents must be ignored.
Matches follow Find's non-overlapping, line-local rules; only complete
matches inside the range are returned. This is a synchronous API, not a
background search service.

The Editor application uses this API to show up to 1,000 match outlines
and a live count for documents up to 2 MiB. These limits belong to that UI,
not to the kit's document engine. Find Next and Previous still work above
the live-count size limit. Select All Matches refuses more than 1,000 matches
without replacing the existing selection.

## Text and ownership rules

- Positions are UTF-8 **byte offsets**, not character counts or screen
  columns. Use the document's boundary and caret helpers for navigation.
- Text must be valid UTF-8 without NUL. LF, CRLF, lone CR, and mixed line
  endings are preserved. The application chooses what Return inserts with
  `tiku_codeview_set_newline()`.
- A failed document transaction leaves its text and history unchanged.
  Check results for read-only refusal, size limits, and allocation failure.
- Undo history is shared by views of the same document. Closing a split
  view does not discard the document while another reference remains.
- Read-only is a **document** property. Two views of one document cannot
  currently have independent editing permissions.
- Use document/view APIs on the owning UI thread. To analyse text in a
  worker, make an owned snapshot on that thread, send the snapshot to the
  worker, and verify its generation before applying the result. Free it
  with `tiku_edit_snapshot_free()` when finished.
- A completion list copies its strings. Its entries must be non-NULL.
  Refused list updates keep the previous list.
- Saving is application work. Do not mark a document saved until the write
  succeeds; retain the history-state identifier for the bytes written.

The kit handles committed UTF-8 input. That is not a claim of full Unicode
editing: grapheme-cluster navigation, bidirectional layout, complex-script
shaping, and IME preedit display need further work.

## Limits

- Regular expressions are POSIX extended, not PCRE: no `\d`, lookaround
  or lazy quantifiers.
- Search, Replace All, match collection and lexers run synchronously on
  the UI thread, with no cancellation; the kit starts no worker.
- Zoomed text uses display-list face IDs 64 through 95: rebuild the
  application and the desktop together.
- Every style keeps the view's font size and cell metrics.
- There is no language server, snippet placeholder, overload selection,
  fuzzy completion or saved shortcut table.
- Grapheme clusters, bidirectional text, complex-script shaping and IME
  preedit display are not handled.

## Build and tests

From the `applications` directory:

```sh
make build/lib/libtiku_editor.a
make build/tests/test_codeview_edit build/tests/test_codeview_find
make build/tests/test_codeview_extensions build/tests/test_codeview_regex
make build/tests/test_codeview_advanced build/tests/test_codeview_tools
build/tests/test_codeview_edit
build/tests/test_codeview_find
build/tests/test_codeview_extensions
build/tests/test_codeview_regex
build/tests/test_codeview_advanced
build/tests/test_codeview_tools
```

Link consumers with `LIB_EDITOR` before the existing desktop kit libraries,
as the Editor and IDE rules in `mk/apps.mk` do. Keep file/project/device
dependencies in the application, not in the editor library.

See the [behaviour contract](../../docs/contracts/editor.md) for the
supported rules and named regression tests.
