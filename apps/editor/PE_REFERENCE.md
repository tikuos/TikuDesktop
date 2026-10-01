# Pe reference audit

Reference: `temp/Pe-master.zip` supplied in the tikuOS workspace (archive
revision `92f20d87533382c5fa14721cb2bd8dd03ae5cbc2`). Reviewed its edit-window,
editing, search, menu and file-options manuals, screenshot `screen01.gif`,
and `Sources/PTextBuffer.h`.

Pe is a useful interaction reference, not a drop-in TikuDesktop application:
it uses Haiku/BeOS APIs and C++. Some source files carry a four-clause BSD
notice, including an advertising acknowledgement. This work uses independently
written native TikuDesktop code; it does not copy Pe source or artwork.

## Adopted in this revision

- Slim, hover-raised icon toolbar: New, Open, Save, Undo, Redo and Find.
- Filename in the toolbar, compact caret-position and encoding panes, and
  horizontal scrolling on the bottom edge. No permanently expanded search shelf.
- A separate Find/Replace window, case and whole-word controls, Replace All as
  one undo transaction, and a separate Go to Line window.
- Dynamic lines and line storage. The earlier 4,000-line, 1,023-byte-per-line
  and 4 MiB file bounds are removed from Editor. No fixed-size staging arrays
  are used for document text. Undo uses transactional range splices.
- Viewport-only text drawing, cached line widths and linear long-line hit
  testing. Tab expansion affects display, never file bytes. Show Invisibles
  can mark tabs and spaces.

Pe itself uses a gap buffer (`PTextBuffer.h`); our editor uses dynamic line
storage and range-edit records. Both avoid the old tiny static line buffers,
but they are different implementations. TikuBasic and other consumers of the
legacy shared textview have deliberately not been migrated in this change.

## Additional Pe features worth considering

| Priority | Pe feature | Why it would help TikuDesktop |
| --- | --- | --- |
| Next | Soft wrapping and per-document tab width | Notes, logs and long configuration lines |
| Next | Auto-indent, shift lines left/right, comment/uncomment | Everyday BASIC/C/config editing |
| Next | Double-click words, triple-click lines, word navigation | Faster selection and editing |
| Next | Bracket matching and function/heading navigation | Navigating device scripts and source files |
| Next | Regex search and a results list | Logs and structured text |
| Later | Multi-file/directory search with filters | Finding settings and code across a workspace |
| Later | Split views of the same document | Comparing distant portions without another copy |
| Later | Configurable keybindings and richer per-file options | Familiar keyboard workflows and persistent choices |
| Later | Recent files, Save a Copy, open selected paths | Less repetitive file navigation |
| Later | Drag/drop text and case/line transformations | Editing convenience |

Pe also has group/project windows, encoding conversion, shell worksheets,
extensions and historical FTP/BeIDE integration. These are not automatically
appropriate to port: TikuBasic already owns projects, and remote-device editing
should follow TikuDesktop's device/VFS access model rather than adding FTP.

## Remaining boundaries

Documents are limited by allocation success and representable byte/line indexes
(signed 32-bit coordinates; the loader rejects files above INT_MAX - 1 bytes),
not the former 4,000/1,023 constants. Undo keeps at most 256 complete changes
and targets 32 MiB of history, retaining the newest whole change even when it
alone is larger. Out-of-memory failures must leave the document unchanged.
The desktop-wide clipboard has a separate limit of 16 MiB.

UTF-8 and consistent LF/CRLF endings are supported. Case-insensitive matching
folds ASCII letters; full Unicode case folding, grapheme navigation, other
encodings, soft wrapping and regex search are not claimed here.
