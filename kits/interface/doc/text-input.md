# Committed text and input methods

`tiku_text_input.h` separates committed UTF-8 text from shortcut keys. The X11
host opens an input context, filters composition events and uses UTF-8 lookup.
It negotiates input-method-managed feedback, falling back to local Compose if
the configured service is unavailable. Ordinary ASCII typing and shortcuts
keep their existing key-event path.

## Using the adapter

Keep a zero-initialized `tiku_text_input_t` per editing target. Feed events to
`tiku_text_input_event()`. Only `TIKU_TEXT_COMPLETE` permits insertion;
`TIKU_TEXT_PENDING` must not change the document or trigger a command.
`TIKU_TEXT_REJECTED` should show an appropriate input error. Key-down, pointer-down
and activation changes discard any incomplete commit. Reset the state explicitly
when replacing a document or changing an editing target without an input event.

Producers use `tiku_text_input_set()` followed by `tiku_text_input_next()` until
there are no more events. A commit is limited to 1023 bytes, not characters.
The staging state costs about 1 KiB. Each event holds up to seven bytes at whole
codepoint boundaries; the existing event size and prior event IDs are unchanged.
For `TIKU_EVENT_TEXT_INPUT` only, `button` contains BEGIN/END flags, `dx` the byte
offset and `dy` the total byte length. `key` and `modifiers` must be zero.
An oversized native commit generates a rejection event, never a truncated edit.
Malformed UTF-8, embedded NULs, misplaced chunks and shortcut-bearing chunks are
rejected. Newline and tab are allowed in the transport, but not single-line fields.

`tiku_field_event()` assembles commits and records one undo snapshot on insertion.
Legacy field users can assemble first and call `tiku_field_commit()` for atomic
replacement. `tiku_textview_commit()` validates UTF-8 and line/count limits before
replacing a selection and groups the replacement as one undo operation. Existing
byte-based undo must also fit its temporary joined lines: a replacement across
near-full lines can therefore be refused even when its final lines would fit.
Such rejection preserves the document and selection. Existing
document allocation-failure behaviour is unchanged; this is not a transactional
out-of-memory guarantee. Caret columns remain byte offsets; navigation and deletion
step whole codepoints, not grapheme clusters.

Append-only legacy fields can use `tiku_text_input_append()` and
`tiku_text_input_backspace()` to preserve UTF-8 boundaries without changing their
buffer layout. Append validates the whole single-line commit and capacity before
changing anything. The legacy field model's Paste action now uses atomic
`tiku_field_commit()` too: invalid, forbidden or oversized text leaves the original
selection intact rather than filtering/truncating it into a different name.

## Current coverage and limits

- TikuDesktop forwards text to focused embedded and remote applications and
  Devices Preferences, bypassing menu/shortcut dispatch. Pointer clicks, native
  focus loss and shell window-focus changes reset composition.
- TikuBasic accepts commits in its document editor, Find/Replace, watch field
  and interactive answer field. TextEdit accepts document commits. Terminal
  forwards committed UTF-8 to its input stream; its rendering is still cell/byte
  oriented.
- TikuDesktop's legacy fields now accept complete commits: Save-panel names,
  folder rename and value editors, Get Info rename, Select by Pattern, Find's
  name/attribute/formula fields, the older quick-find buffer and described-form
  entries. A commit edits only; it never confirms a rename, saves a file, runs a
  query or writes a form's bound device value. Existing confirmation keys retain
  those responsibilities. Save names also respect the smaller panel-model limit.
- Standalone TikuTracker accepts commits in rename and location fields. The shell
  cancels composition when either editor starts. The desktop checks focus across
  chunks and suppresses legacy input behind alerts and popups; folder value
  editors, like rename editors, are bound to their owning folder.
- This migration does not give append-only fields selection or undo history;
  those still need the richer field adapter. Type-ahead lists and other unmigrated
  custom widgets still use their existing key handling.
- Input-method feedback is managed externally, not drawn inline at the caret.
  Surrounding-text exchange, caret-positioned candidate windows, grapheme editing,
  bidirectional layout, font fallback and native accessibility are separate work.
- An input-method disconnect clears the context; the next native focus activation
  tries to open it again. If a local fallback is already active, restart the host
  to select a newly started external service. Actual service support depends on
  its advertised XIM styles. This batch tests local Compose, not every IME service.
- The neutral adapter also compiles in the board toolkit. This does not add an
  input-method engine or change a device's firmware input protocol.

The host follows the input-method contracts in the
[Xlib manual](https://www.x.org/releases/current/doc/libX11/libX11/libX11.html).

## Verification

`build/tests/test_text_input` covers framing, limits, malformed/interrupted input,
field undo and codepoint navigation, document replacement/undo/redo and rejection
without selection loss. `test_client_wake` exercises commits over the actual app
socket protocol; `test_ide_workflow` exercises document and Find field routing.
`test_legacy_input` exercises the actual desktop text router and legacy window
controllers, including focus changes, modal isolation, field limits and no writes
on text entry. `test_text_input` also covers append-only buffers and atomic Paste.
`make test-host-clipboard` uses a private Xvfb/Xephyr display for real X11 clipboard,
Compose and direct Unicode-key input tests. It never alters the user's keymap or
clipboard. `make check-toolkit` checks the displayless/board build too.
