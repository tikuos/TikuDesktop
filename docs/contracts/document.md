# Document Kit behavior contract

This contract covers the C document model, layout and view controller in
`kits/document`, with native font and document-file adapters in Translation Kit.
It distinguishes tested model behavior from the rich editor and file
features that still require implementation.

## Implemented behavior

### External HTML clipboard

Interface Kit holds UTF-8 fallback text and up to four ordered binary MIME
representations within a combined 16 MiB ceiling. Multi-format publication
validates sizes, names and fallback text, allocates complete copies, then
advances one stamp. Duplicate types, malformed MIME tokens, malformed fallback
and allocation refusal preserve the preceding store. Inputs may alias it.
Text-only replacement clears all binary forms. Existing single-format getters
continue to return the preferred first representation.

Application Kit PROBE version 1 advertises a `formats` capability. Version-2
SET carries repeated MIME/data pairs and acknowledges only after publication;
older peers retain version-1 SET/GET. GET intentionally returns one preferred
type with its fallback. TikuWrite publishes native fragments or cell selections
first and HTML second. Both derive from the captured selection, with resolved
styles and embedded PNG images. Cut still requires matching confirmation and
unchanged source state. Failure preparing any representation does not cut.

X11 negotiates up to 1,024 TARGETS atoms, reads UTF8_STRING/STRING fallback,
then advertised `text/html`, including INCR transfers. It stages both until
completion, checks combined size and owner identity, and publishes once. A
refused TARGETS negotiation retains the text path. Wrong property types, owner
loss, timeout or oversize abort the import. A required-input failure clears
stale cached content; an unsolicited refresh failure retains it. Local copies
cancel outstanding imports. Owned HTML is immutable during transfer and is
removed by a later plain copy. No host ownership echo occurs on import.
Paste joining a background refresh promotes it to a required transfer, including
its failure handling. Imports have a one-second inactivity timeout and a
five-second total limit across TARGETS, fallback and HTML chunks.
Preparing a local native offer allocates both text and HTML before replacing
previous bytes. Either allocation failure leaves that preceding offer intact.

Cocoa writes one pasteboard item with String/HTML types and reads both from the
first external item, checking its change counter and complete sizes. It does
not join multiple items. This backend and its unique-pasteboard regression
target require native macOS verification; Linux results do not qualify AppKit.
An unsuccessful background read does not advance the imported change count;
explicit Paste retries it and cannot reuse stale cached contents.

TikuWrite accepts UTF-8 HTML as flat formatted paragraphs or one complete
table, including nested tables, using the existing checked markup importer.
Scratch and imported model each have a 64 MiB ceiling. Unsupported mixed
top-level structure, malformed input and flat content targeting a cell
rectangle are refused without mutation. Active content is omitted with a
reported conversion notice; no external resources are fetched. Empty flat
content is a no-op, not a deletion. Native envelopes remain preferred within
one desktop. RTF and non-UTF-8 HTML are not supported; conversions are bounded
but synchronous, without a clipboard latency guarantee.
Edit / Paste as plain text (Ctrl/Command-Shift-V) explicitly uses the UTF-8
fallback, including TSV into selected cells. It ignores even malformed rich
data but retains generation, selection, read-only and empty-paste guards.
Both paste shortcuts request fresh native clipboard content before delivery.

`test_clipboard_typed` covers three allocation-refusal points for a two-format
publication, aliasing, protocol compatibility, duplicates and combined limits.
`test_host_clipboard` exercises real isolated X11 peers, direct/chunked HTML,
HTML-only owners, staging, wrong types, timeouts, ownership and export clearing.
`test_clipboard_offer` refuses both local offer allocations independently and
checks preservation; it also runs under sanitizers with leak detection.
The clipboard bridge/store also run under ASan/UBSan; native display-library
leak detection is disabled in that lane. `test_write` checks formatted HTML,
native preference, copy round trips, table insertion, malformed/unsupported
input refusal, content omission, empty-paste safety and old-host fallback.
It also checks explicit plain paste from a malformed rich selection.

### Named bookmarks and internal link destinations

`tiku_richbookmark.h` exposes document-owned point bookmarks, separate from
temporary registered anchors. Names are exact, case-sensitive UTF-8 strings of
1–63 bytes, excluding ASCII controls. At most 1,024 names are accepted. Set is
an undoable upsert; removal is undoable; unchanged sets do not publish. The
document read-only, generation, notification and allocation guards apply.
There is no rename API or range-bookmark representation.

Snapshots share immutable bookmark arrays. Text and structural edits map their
points through the existing anchor maps before history preparation. BEFORE
and AFTER insertion affinity is retained; deletions collapse points to valid
surviving locations. Undo/Redo restores each version's metadata without
allocation, including points in removed cells. Bookmark arrays participate in
the same unique-storage history accounting as text and table structures.

Native schema 1 declares `bookmarks` and writes a final `bookmarks` container,
following `body` and any `structure`. It contains 1–1,024 `bookmark` elements
with exactly `name`, zero-based source `paragraph`, `byte`, and `after`
attributes. Source indices become new model IDs at import. Unknown extensions,
undeclared containers, duplicate names and non-boundary positions are refused.
Decoder staging and model ownership are budgeted separately; parsing,
conversion and serialization support cancellation. Old files remain valid.

`tiku_richsnapshot_resolve_link` accepts validated `#name` targets, percent
decodes the name, and returns its copied position without allocating. Missing
targets return NOT_FOUND and external URLs return UNSUPPORTED. There is no
Unicode normalization, browser launch or external resource access. TikuWrite's
View menu adds named-point panels, seven quick-name slots and Follow document
link. Navigation checks generation, waits for layout and snaps scalar points
to grapheme boundaries. Read-only navigation is permitted; stale editing
panels refuse changes.

Native files and recovery preserve definitions. Clipboard fragments do not
copy definitions; they retain link strings. HTML and Markdown export report
omitted bookmark targets. PDF named destinations and mouse-click activation
are not provided. `test_richbookmarks` covers generated anchor equivalence,
grouped typing, tables, names/limits and exhaustive mutation-allocation
refusals. Native fault tests include bookmark staging/model allocation.
`test_richbookmarksio` covers schema and ZIP round trips, malformed input,
cancellation and export reports. Application and desktop-script tests cover
the menu/panel, internal-link, Undo and save/reopen workflows.

### HTML export adapter

Translation Kit's `tiku_richhtml_write()` exports a snapshot without modifying
its document or touching files. It produces bounded, NUL-terminated UTF-8
HTML with resolved styles, links, list numbering, nested tables, column
weights, header groups, borders and cell shading. Text/attributes and CSS
font-family names use separate escaping. No scripts or external resources
are emitted. Formatting uses locale-independent decimal values.

The native codec options apply, with `xml_bytes` interpreted as the HTML byte
limit. The output and terminator also fit within half the scratch budget.
One counting pass precedes the output allocation; cancellation is checked
during both passes. Failure clears the buffer, byte count and notice bits.
The input snapshot may outlive its original document.

Pagination is browser-controlled and always reported as a conversion notice.
Named styles are flattened to resolved CSS with a corresponding notice.
Pictures are embedded as PNG data URLs with straight alpha, point-valued
display dimensions, escaped alternate text and any enclosing hyperlink.
The image notice bit is reserved and not emitted. Exact rendering equivalence
is not provided. TikuWrite exposes File / Export HTML,
uses guarded atomic replacement, reports losses, and retains its original
path and saved/unsaved state.

`test_richhtml` covers escaping, explicit style resets, nested lists/tables,
header groups, dimensions, transparent colours, empty paragraphs, limits,
locale independence and cancellation at each observed poll. An independent
Python HTML parser checks balanced nesting and decoded values. The native
fault suite refuses the output allocation and checks ownership. TikuWrite
tests cover export, external conflicts, extension checks and unchanged source
state; `writehtml.script` and `writehtmlpicture.script` exercise the desktop
menu and file panel.

The allocation-free PNG callback encoder uses stored DEFLATE blocks of at
most 4,096 raw bytes. Its size query lets the HTML counting pass account for
base64-expanded pictures before allocating output. Repeated occurrences
count separately. Output exceeding configured limits is refused, not
converted to alternate text. Callback refusal stops streaming immediately;
the HTML caller discards partial output. The existing compressed file/stream
PNG writer is unchanged.

`test_png_stream` compares 45 decoded pixel arrays, exact output sizes and
refusal at every observed callback, including rows crossing DEFLATE blocks.
The independent HTML parser additionally checks PNG chunks/CRCs and zlib
scanlines for alpha, all base64 padding cases, tall/wide pictures and links.
HTML tests walk cancellation points in a multi-picture document and verify
that an output-limit refusal performs no allocation.

### Editable markup adapters

Translation Kit's `tiku_richmarkup_read_html()` builds a new, checked document
from bounded UTF-8 HTML. It reads paragraphs, six semantic heading levels, inline
formatting, links, lists, preformatted text and rectangular tables nested up
to eight deep. Optional paragraph, list-item, cell and row end tags are
accepted. HTML text whitespace collapses unless PRE or supported inline CSS
requests preservation. HTML export supplies that whitespace rule inline.
Character references, CRLF/CR in preformatted text and empty paragraphs are
handled explicitly.

The importer does not execute scripts or fetch resources. Active elements are
omitted with a notice. Embedded `data:image/png;base64,` pictures become owned
inline images with their alternate text, enclosing link, and point/px CSS or
integer HTML pixel dimensions. One specified dimension preserves aspect ratio.
External URLs and unsupported image variants become alternate-text labels;
no local or remote resource is opened. Unsupported CSS, stylesheet rules, unknown structure and
page-layout loss produce notice bits. Rowspan/colspan greater than one and
irregular table grids return UNSUPPORTED, not a partial document. These
adapters do not promise browser layout or a reversible HTML round trip.

`tiku_richmarkup_read_markdown()` uses the documentation Markdown subset in
semantic document mode: six heading levels, explicit decimal item values and
up to nine list levels. Raw HTML remains literal text. The SYNTAX notice is
always set because this is not CommonMark. `tiku_richmarkup_write_markdown()`
exports headings, lists, bold, italic and links. Tables become paragraphs,
pictures use alternate text, and unsupported formatting/structure is reported.
`tiku_richmarkup_report()` formats the notice bitmask for the caller.

Import staging uses checked growth and charges old and replacement arrays
simultaneously. Strings, blocks, paragraphs, runs and final source arrays
share the scratch budget; the returned model has its separate allocation
ceiling. Cancellation is polled through token scanning, conversion and
staging. Failure clears outputs and notices. New documents have no retained
source pointers and no editing history.

PNG import checks its signature, dimensions and total pixel count before
decoding. It allows at most 4,194,304 pixels per picture. A conservative
preflight bound covers compressed-data growth, decoded pixels, inflated rows
and decoder scanlines while the encoded input and previous images remain
charged. Import staging also counts retained image metadata and pixels.
The native model has its separate aggregate limit. Malformed base64/recognized
PNG input refuses the import, as do memory limits or allocation failure.
Cancellation is checked between base64 chunks and before/after each PNG decode;
the existing decoder has no mid-inflate cancellation hook. Unsupported 16-bit
variants and non-palette `tRNS` transparency use alt text with an image notice
rather than losing transparency without a report.

### Semantic headings and navigation

`TIKU_RICH_OUTLINE` carries a paragraph level of 0 (body) or 1..6 (heading).
The value inherits through paragraph styles, supports direct clearing, mixed
selection queries and ordinary undo/redo. Naming a style `Heading 1` alone
does not set a semantic heading level. Native schema 1 declares the required
`outline-levels` feature whenever a paragraph or style explicitly supplies
the property, including an explicit zero. Undeclared attributes, unrecognized
property bits, noncanonical absent values and levels above six are refused.
Rich clipboard and recovery reuse this representation. HTML emits `h1`–`h6`;
Markdown emits the corresponding number of `#` characters.

`tiku_richoutline` incrementally scans a retained immutable source without text
layout. Its arrays are charged to the document allocation owner, including
growth peaks. Partial entries are queryable while pending; failure is latched
and must not be presented as completion. Results contain a borrowed UTF-8
label, paragraph index, stable position and resolved heading level. The index
includes empty headings and headings in nested cells. Previous/next lookup
does not wrap. Apps select with the captured generation to refuse stale
navigation. TikuWrite supplies a virtual heading sidebar, keyboard and wheel
navigation, and previous/next commands independent of sidebar visibility.

TikuWrite opens .html/.htm/.md/.markdown through its existing isolated worker
and version checks. It adopts only a complete result with an unchanged target
document. Converted input requires Save As .tkrd; source files are not silently
overwritten. Markdown export uses the same guarded atomic replacement as
other exports and never changes native saved state. The Open picker starts in
the native document's directory or the user's home. Tracker associations are
unchanged.

`test_richmarkup`, `test_richio_faults` and `test_write` cover semantic imports,
whitespace, ownership, byte and allocation ceilings, cancellation, malformed
input, conversion notices, original-file protection and export conflicts.

### Core model and view

| Area | Contract | Test suite |
| --- | --- | --- |
| Lifecycle | Creation produces one empty paragraph; failure clears the output handle | `test_richdoc_model`, `test_richdoc_faults` |
| Import | Input is copied and checked before replacement; rejection preserves the current state | `test_richdoc_model`, `test_richdoc_faults` |
| Text | Raw paragraph input rejects malformed UTF-8, NUL, CR, disallowed ASCII controls and unrecorded object markers | `test_richdoc_model`, `test_richdoc_edit` |
| Runs | Positive lengths cover the paragraph on UTF-8 boundaries; absent runs create one unstyled run for nonempty text | `test_richdoc_styles` |
| Styles | Explicit presence, same-category inheritance, unique names, bounded acyclic chains and finite values | `test_richdoc_styles` |
| Identity | IDs are document-local and not recycled by imports; exhaustion refuses publication | `test_richdoc_faults` |
| Positions | Body, paragraph, byte boundary and affinity are checked; comparisons use paragraph order | `test_richdoc_model` |
| Snapshots | Immutable text and styles survive edits and live-document release; workers can read and release snapshots | `test_richdoc_model`, `test_richdoc_edit` |
| Replacement | Paragraph-local replacement retains its ID, prefix/suffix formatting and defined typing inheritance | `test_richdoc_edit` |
| Formatting | Direct range formatting preserves explicit false, rejects split UTF-8 scalars and merges equal stored runs | `test_richdoc_edit` |
| Paragraph properties | Alignment, indents, spacing, keep rules and default character values are undoable; clearing restores inheritance, including in empty paragraphs | `test_richdoc_formatting` |
| Editable styles | Create/update/remove share transaction history; parent edits validate descendant depth; assignments preserve direct overrides; removing a referenced style is refused | `test_richdoc_formatting`, `test_richdoc_faults` |
| Search | Budgeted, case-sensitive literal search returns nonoverlapping, snapshot-relative ranges across runs, soft breaks and paragraphs; stale replacements are refused | `test_richdoc_search`, `test_richdoc_faults` |
| Lists | Bullets and decimal lists use levels 0–8; restart/continuation, shallower-level resets and non-list boundaries determine numbering; split continues and join keeps the leading membership | `test_richdoc_lists`, `test_richdoc_faults` |
| Hyperlinks | Validated targets are owned separately from appearance; editing edges do not extend links; linked replacement, formatting and structure preserve supported link semantics | `test_richdoc_links`, `test_richdoc_faults` |
| Selection formatting | Allocation-free queries distinguish uniform absence, explicit false and mixed values; half-open endpoints, caret inheritance and link edges have defined behavior | `test_richdoc_selection`, `test_richdoc_faults` |
| Fragments | Captures own selected text/runs and only required styles; they survive source closure without retaining its root; fragment snapshots cannot mark a document saved | `test_richdoc_fragments`, `test_richdoc_faults` |
| Styled paste | Cross-document paste remaps IDs/styles, reuses equivalent definitions, renames conflicts and copies links into destination ownership; surviving boundary text and copied text retain character appearance | `test_richdoc_fragments`, `test_richdoc_faults` |
| Commands | A 36-entry typed catalog shares query/dispatch across hosts; queries allocate nothing; mixed/explicit/absent values remain distinct; document and host read-only flags and stale generations are rechecked at execution | `test_richcmd`, `test_richdoc_faults` |
| Command ranges | Character commands skip empty slices; paragraph commands exclude an ending paragraph at byte zero; multi-paragraph edits publish once; list restart applies only to the first selected item | `test_richcmd`, `test_richdoc_faults` |
| Sessions | Two retained anchors track an independent directional selection; caret formatting is local, allocation-free and does not dirty content; queries include pending values | `test_richsession`, `test_richsession_faults` |
| Session history | Commands publish collapsed or retained selections atomically; undo/redo restore the initiating session even after navigation; other views follow position maps; closed session IDs are not reused | `test_richsession`, `test_richsession_faults` |
| Notifications | Ordered synchronous callbacks observe fully published content and selections; reentrant mutations return BUSY, void mutations/releases do nothing; subscriptions retain their document | `test_richwatch`, `test_richsession_faults` |
| Structure | Split keeps the leading ID and assigns a fresh trailing ID; join retains leading paragraph properties without changing surviving character appearance | `test_richdoc_structure` |
| Range edits | Cross-paragraph replacement and formatting commit as one group; plain-text paste normalizes CRLF, CR and LF into paragraph boundaries | `test_richdoc_ranges` |
| Tracked positions | Independent anchors follow affinity, structural edits and history; unchanged anchors regain exact pre-edit positions on undo | `test_richdoc_anchors`, `test_richdoc_ranges` |
| Transactions | Sequential operations publish as one group; a later failure does not expose earlier operations | `test_richdoc_edit`, `test_richdoc_faults` |
| History | Undo/redo exchange roots without allocation; branch edits discard redo only on success | `test_richdoc_edit`, `test_richdoc_faults` |
| Save state | Saving an older snapshot does not mark newer text clean; undo to the saved state does | `test_richdoc_edit` |
| Budgets | Aggregate includes temporary candidates and snapshots; history counts unique allocations and reserves its maximum extra cost across reachable versions | `test_richhistory`, `test_richdoc_faults`, `test_richdoc_edit` |
| Isolation | Unchanged paragraphs are shared; changed paragraphs and style-name storage have valid independent ownership | `test_richdoc_edit` |
| Typing groups | Compatible adjacent session insertions share one undo group; saved states, navigation and other edits end groups; failed merges preserve history | `test_richdoc_typing`, `test_richsession_faults` |
| Segmentation | Unicode 17 default grapheme and word boundaries; invalid input preserves output | `test_richtext` |
| Layout | Budgeted cancellable construction, compatible paragraph reuse, mixed baselines, wraps, tabs, lists, shared hit/caret/selection geometry and retained source lifetime | `test_richlayout`, `test_richlayout_cache`, `test_richlayout_faults` |
| Views | Independent sessions/viewports, grapheme navigation, scroll/zoom, invalidation on edits, no stale geometry and fresh-layout equivalence | `test_richview`, `test_richlayout_faults` |
| Native drawing | Checked glyph IDs, scaled clipping, font lifetime, explicit missing-font/shaping diagnostics | `test_richfont` |
| Host input | Shared shortcuts, complete commits, pointer selection and Unicode-safe deletion; stale transfers are refused | `test_richview_input` |
| Native files | Schema-1 `.tkrd` preserves current paragraph semantics; unknown content is refused, not dropped | `test_richio_native` |
| Fragment wire format | Bounded versioned envelope preserves selected styles, links and pictures; untrusted input creates an independently owned fragment or clears output on refusal | `test_richclip`, `test_richio_faults` |
| File failure | Allocation failure and partial ZIP output release temporary state; failed pre-rename saves preserve the destination | `test_richio_faults`, `test_atomic_file` |
| ZIP output | Stored ZIP32 with CRCs, prevalidated names and lengths; package reading bounds directory allocation | `test_zip_write` |
| Whole-word search | Unicode word and grapheme boundaries at both ends, incremental preparation, exact paragraph context, no allocations while stepping | `test_richdoc_search`, `test_richdoc_faults` |
| Paper | Validated single-section dimensions/margins survive snapshots, history and native encoding | `test_richpages`, `test_richoutput` |
| Page breaks | Direct/inherited breaks override keep-next, never add an initial blank, and stay on the leading split paragraph | `test_richpages`, `test_richoutput` |
| Pagination | Bounded layout/group/line work; complete line coverage, keep diagnostics, no publication on limit or allocation failure | `test_richpages` |
| PDF and preview | Matching retained glyph geometry, restored preview clip, embedded font bytes, PDF text/page size/count, cancellation | `test_richoutput` |
| Guarded replacement | Two version checks, pre-rename rollback, private new files, distinct conflict and post-rename uncertainty | `test_atomic_guard` |
| TikuWrite | Rich editing/styles, refused Cut, snapshot saves, conflicts, explicit recovery, small-window layout, preview and export | `test_write` |
| Plain text import | UTF-8 and line-ending validation, BOM handling, 4,001-paragraph import, scratch/model budgets, failure ownership | `test_richio_native`, `test_richio_faults` |

The model suite checks 2,000 generated imports against an independent text
oracle. The editing suite checks 3,000 generated replacements and their
undo/redo results against an independent string model. The range suite adds
2,000 generated multiline edits with eight independently positioned anchors.
The search suite compares 4,000 generated full-document and restricted-scope
searches with an independent flat-string oracle, including multibyte UTF-8,
soft breaks and paragraph boundaries.
The list suite compares 1,000 generated membership/edit cases with an
independent backward-numbering recurrence. Linked snapshots are read and
released on four workers during live edits and document closure.
The fragment suite adds 1,000 generated capture/self-paste cases with an
independent flat-text and eight-anchor oracle. Character appearance is checked
against the original selected and surviving text. It also checks Unicode style
conflicts, a 1,100-paragraph paste and bounded capture from a many-run paragraph.
Failure injection walks allocation points in creation, import, snapshots,
multi-operation edits, split/join, range replacement/formatting and anchor
creation, including mapping/history records. It also covers paragraph/style
commands, list/link commands, linked imports and search construction.
Fragment capture, fragment snapshots and rich paste are covered too, including
independent source/destination allocators, copied targets, style remapping and
all destination content/history ceilings. It exercises every allocating command
in the catalog and named-style update through the dispatch path. The fault
suite injects 485 failed allocations; undo/redo, selection queries and command
queries allocate no memory.
History accounting uses allocation identity, including shared image pixel
storage and link targets. Its reserve is the union of allocations reachable
from the current and retained roots, plus records/maps/bindings, minus the
smallest root's unique footprint. This bounds the extra history cost at every
reachable Undo/Redo state without allocating during an exchange. The
`history_retained_memory` query subtracts the current root instead. Snapshot
ownership is not subtracted; the total-memory query includes those readers.
The allocation-header census uses balanced owner-thread counters, no heap
scratch and no content mutation. Cached root footprints are owner-thread-only.
Each edit scans candidate roots and the newest retained prefix; unchanged
paragraph payloads are traversed once per census. Work still scales with
retained root/paragraph arrays, not just edited bytes.

`test_richhistory` compares both counters with an independent allocator
registry that identifies whole allocations from interior pointers. It checks
shared links, pixels and structures, separately allocated identical pictures,
styles, typing coalescing, snapshots, maximal newest-prefix retention, redo
branching, exact budget boundaries and failed-edit retries. No census state
survives a failed publication. With no external retained readers, clearing
history frees exactly the additional bytes reported for the current state.

Command tests also compare 1,000 generated formatting/clear/toggle selections
against an independent per-character presence/value oracle, checking query
states and undo/redo. Command metadata, permission/staleness, payload refusals,
paragraph endpoints and transaction-count boundaries have explicit tests.
Refused operations preserve
live roots, IDs, generation, redo, memory charges and tracked positions.
Session tests compare 1,000 three-view multiline replacements and undo/redo
cycles with an independent flat-offset oracle. Tests cover reversed selections,
manual navigation, late/closed sessions, read-only views, UTF-8 boundaries,
pending style removal, import and rich paste. Another fault suite injects
51 allocation failures in construction, typing, range formatting and paste.
It preserves exact session/anchor state and existing redo on refusal, observes
no partial-change callbacks, and checks history-budget failure. Notification
tests attempt each mutation family during delivery and check post-publication
selection, metadata events, observer order, retention and registration limits.

## Ownership and publication

Live document calls belong to one owner thread. A caller must hold a valid
document reference for the duration of each call. The kit does not inspect
operating-system thread IDs or make concurrent mutation safe.

Snapshot nodes and allocation owners use atomic reference counts. The
snapshot handle itself has one release obligation. Queries return borrowed
immutable pointers tied to its lifetime. Custom allocator contexts must
outlive those pointers, surviving searches and fragments. Their hooks must
support allocation/release on a worker when the host creates searches or
fragments there.

Search handles retain a root independently of the snapshot handle and copy
the search term. Prefix indexing is linear in term length; incremental
scanning performs no allocations, includes fallback comparisons in the
work budget and has linear overall scan cost. A handle has one calling
thread at a time. Releasing it cancels its work; it has no callbacks or
background jobs to join. Results identify their captured generation, so
replacement can reject live content that changed after the search began.

An edit creates an unpublished root and replacement paragraphs, checks
resulting run/text limits, and allocates its history record before changing
the live document. Failed edits preserve both undo and redo. Generation
increments only on publication or a successful history exchange. Import is
a separate load operation: it clears history and establishes a clean state.
It also moves live anchors to the new document's first position.

Anchors are owner-thread objects retaining their document. History captures
positions by non-reused anchor identity and explicit-movement revision.
An unchanged anchor can recover its exact pre-edit location; a manually moved
or later-created anchor instead follows forward/inverse position maps.
An initiating session is the exception: history restores its recorded
directional selection regardless of later navigation. Its two anchors use
non-reused identities, so a closed session cannot transfer that restoration to
a newly opened one. Session typing attributes are stored with the history
entry; other sessions retain their own pending choices unless a referenced
style is removed. Import resets selections and pending attributes in all
sessions.
Range replacement maps the whole selection, not the incidental sequence of
joins and splits used to build the result.

History charges the complete larger root on either side of each group, plus
the record, mapping storage and captured anchor positions.
This deliberately conservative estimate double-counts shared nodes; it is
not the same as actual allocation usage. Oldest groups are evicted whole.
An unrecordable edit is refused. Undo and redo require no allocation.
Immutable link targets are retained by individual stored runs and included
in aggregate allocation accounting. History conservatively charges shared
targets once per referring run; overflow refuses publication.

List membership is direct paragraph metadata, not text or a named style.
Numbering is derived per snapshot and does not allocate storage. Hyperlinks
store target strings but have no activation callback, network client or
filesystem access. Scheme filtering is a storage rule; hosts still own
navigation authorization and target resolution.

Fragments copy selected text/runs and style ancestry, retaining shared link
strings but not the source root. Fragment snapshots have state token zero
and are not accepted as saved-document checkpoints. Paste copies imported
links into the destination allocator, remaps style references and constructs
one unpublished replacement array. Same-category/name styles are reused only
if their stored properties and remapped parents match; conflicting names get
UTF-8-safe suffixes. Existing destination style definitions remain unchanged.

Paragraph properties follow surviving destination boundary text. When both
boundaries become one paragraph, a surviving prefix takes precedence over
the suffix. Complete imported paragraphs retain source properties. Character
appearance is preserved with direct overrides/reset bits where needed under
changed paragraph defaults. The leading destination paragraph retains its ID;
new paragraphs receive fresh IDs. One flat replacement map and one history
group describe the paste. Empty fragments delegate to plain range deletion.

Command availability is a current-context check, not a dry run of a payload.
Known commands return query status OK with enabled/disabled state and a refusal
code. Unknown commands and invalid API handles fail the query. A stale query
does not report a checkbox value from the new content. Read-only queries can
still report formatting when their selection and generation are valid.
Parameter validation and final budget checks occur during execution. Every
dispatch rechecks availability before reaching the model's mutation path.
Global history/style commands ignore selection endpoints; range commands use
ordered live endpoints. A host read-only flag is a UI restriction, not a
security boundary. The stateless command API does not own a selection or
pending formatting. The session wrapper supplies both, with text insertion
using pending character overrides and rich paste retaining source formatting.
Neither API opens URLs, writes files or accesses a system clipboard.

Document observers run after root/history, anchors and session typing state
are published. Delivery allocates nothing and blocks nested document and
session mutation, registration and destruction with BUSY. Existing void
document/anchor release, setter and history-clear APIs do nothing during
notification; the caller
must defer them. Snapshot and formatting reads are permitted. Callbacks borrow
their event descriptor and context; subscriptions retain their document until
removed. Metadata notifications do not advance content generation. Local
selection, permission and pending-format changes do not emit document events.

Each session can independently register one selection observer with
`tiku_richsession_observe_selection()`. It receives old/new directional
endpoints and the current generation, after the endpoints have settled.
Explicit navigation does not advance document generation or notify document
watchers. Published edits, Undo/Redo and imports deliver document events first,
then selection events for sessions whose endpoint values changed. Affinity
changes count; identical selections and refused operations do not notify.
Registration and delivery allocate nothing. Context is borrowed until observer
replacement or session destruction. NULL removes the observer without a final
event. Mutation, destruction and replacement during callbacks return BUSY.
Pending-format and cell-rectangle-only changes are outside this callback's
scope. Native accessibility remains a separate host integration requirement.

`test_richsession_observer` checks navigation direction, same-value suppression,
affinity, independent sessions, typing, foreign edits, undo/redo, imports,
allocation-free registration/delivery and reentrant mutation refusal.
TikuWrite schedules toolbar/menu repaint through this callback.

## Build and verification

```sh
make test-document
make test-document-sanitize
make test-document-files
make test-document-files-sanitize
make test-document-output
make test-document-write
make build/lib/libtiku_document.a build/lib/pic/libtiku_document.a
make build/examples/tiku_document_model
./build/examples/tiku_document_model
```

The dedicated targets do not launch shells or discover serial devices.
The sanitizer target enables address and undefined-behavior sanitizers;
leak detection follows the repository's host setting. Use the
[embedding guide](../../kits/document/README.md) for API examples and limits.

## Not implemented

The view controller, shared input adapter and native surface painter are
usable independently; Notes and TikuWrite supply
an application window host. Input handles committed text, not preedit composition or a
native accessibility bridge. Clipboard transport remains a host responsibility.

TikuWrite's out-of-process pixel frames use the Application Kit's snapshot
transport. With snapshot support negotiated, a shared transfer buffer remains
reserved until the desktop has copied it and acknowledged its publication
token. The compositor retains its own pixels throughout redraws. A burst that
fills both transfer buffers uses copied frame messages. This contract applies
to other pixel-based applications too; `test_remote_snapshot` checks ownership
and delayed acknowledgment behavior independently of document layout.

The native font provider maps cmap glyphs and reports complex-text warnings;
it does not implement OpenType substitution/positioning, kerning or bidi.
Edits operate on the main body and its nested table cells. Header/footer,
footnote and other secondary text bodies are outside the model.

Other unfinished model capabilities include case folding and a general model
replace-all API. TikuWrite implements
bounded Replace All by composing up to 1,024 paragraph-local matches into one
transaction. Native schema-1 files and single-section page layout are
implemented. Office edits, floating pictures, multiple sections,
headers/footers, footnotes and native printing are not. Save conflict detection,
recovery and file panels belong to TikuWrite rather than the codec.
Translation Kit serializes paragraph fragments and validates untrusted
fragment input. TikuWrite transfers them through the desktop's typed clipboard
protocol. Rectangular cell envelopes use a distinct MIME type and checked
table paste. Native-host rich MIME negotiation and HTML/RTF transfer remain
outside this implementation.
List definitions connecting noncontiguous sequences, custom list markers,
external-link activation and automatic link-click UI are outside this API.
Notes embeds Rich View for its plain-text note body. Its files remain plain
text, and formatting shortcuts are disabled. Editor and Office have not been
migrated. TikuWrite is a separate application using the same public kit.
Semantic accessibility and rich clipboard requirements still prevent claiming
the entire application/international-input plan complete.

## Serialized fragment profile

`tiku_richclip.h` defines MIME type `application/x-tiku-richfragment`.
Its version-1 envelope contains:

| Byte offset | Meaning |
| --- | --- |
| 0–5 | ASCII `TIKURF` |
| 6 | Zero |
| 7 | Version, currently 1 |
| 8–15 | Unsigned 64-bit little-endian XML byte count |
| 16 onward | Exactly that many bytes of schema-1 native document XML |

The envelope is binary, has no terminating NUL, and is not a `.tkrd` ZIP.
Version mismatch returns UNSUPPORTED. Wrong magic, missing payload,
truncation and trailing bytes return DAMAGED. The full envelope is capped
at 16 MiB before reading its header. Native XML validation checks features,
UTF-8, style references, link schemes, resource dimensions and pixel counts;
it rejects external entities. A valid table document is refused as
UNSUPPORTED, not flattened into paragraphs.

The writer takes a captured fragment, so source text outside the selection
and unused named styles are absent. All paragraphs in a decoded flat body
form the fragment; one empty paragraph represents an empty selection, while
two empty paragraphs preserve a selected paragraph separator. Paste uses
the existing atomic operation and style remapping; serialized model IDs are
not destination IDs. Embedded pixels survive closure of the source process.
Page setup can be present in the embedded snapshot, but fragment paste does
not apply it to the destination.

Scratch and imported-model defaults are separately bounded at 64 MiB each.
The XML ceiling is the minimum of the requested XML bound, 16 MiB minus the
header, and `(scratch_bytes - 18) / 2`. Decode uses the same ceiling. The
temporary imported document and fragment capture share the model allocator.
The caller owns the input buffer; the caller's existing source model is also
outside codec scratch. Refusal releases intermediate allocations and clears
outputs. Cancellation is polled during codec work and around synchronous
capture, without a latency claim. The custom allocator context must outlive
the decoded fragment and snapshots.

Tests assert independent text/style/image expectations, canonical round trips,
separate-process transfer, source/input lifetime independence, undo/redo,
empty selections, paragraph separators, every truncation, header mutations,
hostile XML, tables, cancellation polls and allocation refusals. Clipboard
publication and application reply guards are tested separately by
`test_clipboard_typed`, `test_write` and `writerichclip.script`. Native-host
rich MIME negotiation remains unsupported.

## Hyperlink occurrence navigation

`tiku_richlinkscan` retains immutable source content and traverses runs under
a caller-supplied work budget. One charged allocation creates the scan;
stepping allocates nothing. Adjacent equal-target runs merge into one
occurrence, regardless of formatting, without crossing paragraph boundaries.
Forward finds a start at/after the source position; backward finds a start
strictly before it. No wrap, fetching, target execution or mutation occurs.
Results expose generation-checked UTF-8 endpoints and a scan-borrowed target.

`tiku_richlayout_snap` expands valid scalar positions to preceding/following
grapheme carets using retained layout geometry. TikuWrite's Previous/Next
hyperlink commands use that geometry for selection while retaining raw
occurrence endpoints for subsequent traversal. Document or selection changes
cancel unfinished navigation. These commands also work in read-only views.
Bookmark management and activating external targets remain separate work.

## Paragraph ruler

`tiku_richruler_read` returns a bounded, owner-thread selection summary in
document points from completed current layout. It reads at most 1024
paragraphs, excludes a final byte-zero endpoint, and refuses different
containing cells or rectangular selections. Geometry includes list label
advance/level spacing and cell padding. Reused paragraph layouts retain those
measurements. Alignment offsets do not change ruler boundaries.

`tiku_richruler_preview` is allocation-free and changes no model state. It
clamps the proposed handle against the intersection of selected paragraphs'
limits, retaining at least one point of text width. Hosts keep the original
capture separate from the displayed preview. `tiku_richruler_apply` checks
view identity, generation, directional selection revisions, width, zoom,
current geometry and read-only state. It allocates a charged edit array and
publishes one atomic undoable transaction, preserving unmodified indentation
components, style references and lists. Failed edits preserve selection,
content and history. Captures must not outlive their view.

Mixed markers display the first selected paragraph and expose a bit mask.
Applying a marker unifies its position, not the selection's other indentation
components. Changed paragraphs receive direct indentation overrides; undo
restores prior style inheritance. Read-only queries succeed with no enabled
handles. This ruler describes continuous editing, not paper margins, tab-stop
editing or vertical page measurements.

## Geometry and view verification

The Unicode suite consumes all 766 grapheme and 1,944 word cases from the
pinned Unicode 17 conformance files. Layout tests compare 1,000 generated
wrap/hit/caret cases with a separate fixed-advance oracle. View tests compare
1,000 resize/zoom results with fresh full-layout jobs, and exercise observer
order, multiple views, stale geometry, selection, navigation and scrolling.
Cached reflow compares geometry against independent full-layout jobs after
edits, splits, joins, undo, style updates, font-generation changes, resizing,
list renumbering, nested-table resizing and picture placement. The reuse key
includes paragraph identity, style definitions, provider identity/generation,
default formatting, leading, tab spacing, available width and list label.
Only completed layouts can provide cached geometry. Old cache references and
lookup storage are released at termination, including failure and cancellation.
Cache lookup probes are bounded; a missed entry uses ordinary reflow.

The view automatically uses the last completed layout and makes at most one
fresh attempt after a cached job's allocation or capacity refusal. The
per-layout storage ceiling excludes the separately retained old layout; the
source owner's aggregate ceiling includes both. Geometry copying, structural
traversal and table row-height calculation still require work. This is not a
constant-time editing guarantee or a native-platform latency claim.

Layout/view fault tests inject 84 allocation failures and cancel jobs at 621
intermediate states, including cache preparation/copying and cold retries.
They check peak allocation limits and limit failures in copied lines.
Session faults include typing-group merges (61 injected
failures). The aggregate document allocation budget includes retained layout
arrays and view state; native font storage has separate file-size limits.

`make test-document-render` tests native glyph IDs and clipped drawing at
three scales in normal and sanitizer builds. This separate target requires
an installed DejaVu Sans or Arial face, or an explicit `TIKU_TEST_FONT` path.
It does not need an X server or access attached boards. Core document suites
do not need an installed font. These are not native-window integration tests.

The native view painter restricts drawing to the caller's viewport and outer
clip, restores the clip, and uses the view's zoom and document scroll. It
paints selection as an ARGB overlay and a caret only for an empty selection.
Pending geometry draws nothing. New selection APIs expose ordered endpoints,
allocation-free plain text, Unicode word segments and wrapped visual lines.

`make test-document-notes` checks the embedding and clipboard publication in
normal and sanitizer builds. Notes tests cover legacy plain files, typing,
word/line/drag selection, whole-grapheme deletion, undo, refused Cut, resize
allocation failure, input ordering during reflow, snapshot-save races and
failed-save retention. A save marks its captured state, not whatever happens
to be live when the worker finishes. The clipboard client refuses size,
allocation, message-field and send failures before replacing its local mirror.

`TRK_SHELL_ONLY=notes.script sh tests/integration/shell/run.sh` exercises Notes
through the real separate-process application runner and an offscreen desktop.
It checks menus, editing, file saves and window controls. This does not qualify
native X11/Cocoa composition, accessibility or native-host performance.

## Inline pictures

An image run covers exactly one U+FFFC token; raw object markers and picture
records covering text are refused. The model copies checked ARGB pixels and
alternate text. Snapshots and fragments retain them independently, metadata
changes share pixel storage, and cross-document paste charges copied
resources to the destination. Images participate in text replacement,
split/join, formatting, undo/redo, saved-state tracking and native persistence.

Rich View selects pictures on click and exposes a bottom-right resize grip.
Dragging changes a preview rectangle; primary-button release publishes one
aspect-preserving image-property command. Cancellation and stale document or
selection state do not publish a resize. Read-only sessions cannot resize.
Text caret movement treats each image as one object, including beside
combining text. Screen and PDF use the image dimensions retained by layout.

`test_richdoc_images` checks validation, copied pixels, shared resize storage,
retained snapshots, cross-document fragments, allocation refusals, undo,
deletion/reclamation, split/join, object geometry and pointer resizing.
The command fault suite also injects failures in image insertion and changes.
`test_richio_faults` includes repeated images in its allocation/write-fault and
4,000-mutation fixture. `make test-document-media` runs native-image round
trips, hostile XML, alpha preview and exported-PDF raster checks in normal
and ASan/UBSan builds. `test_write` covers async insertion, width/alternate
text controls, history, stale targets, protected Cut and native save/reopen.

The native required feature is `inline-images`. XML contains hexadecimal
ARGB samples and exact binary32 dimensions, with no external resource paths.
Repeated payloads use the required `image-references` extension. The writer
groups SHA-256 pixel/dimension keys, verifies exact pixel equality and emits
backward image ordinals. Display size, alternate text and links remain
per occurrence. Hash collisions cannot alias different pixels. Old files
remain readable; readers without the extension must refuse new referenced
files. Standalone pixels keep the earlier encoding without the extra feature.

The reader refuses missing feature declarations, zero/self/forward references,
dimension mismatches and reference child content. It shares decoded staging
pixels until checked model import finishes and frees each owned payload once.
The live model still charges its imported image copies separately. Writer
resource keys/references and style mappings together fit half the codec
scratch budget; encoded XML uses the other half. Hashing and byte comparison
poll cancellation by pixel chunks; sorting has no fixed latency guarantee.
Native save, recovery and rich clipboard use this same codec. This is pixel
deduplication in document.xml, not compressed external media parts.

Limits: 4 Mi decoded pixels per image, finite 1..14400-point dimensions,
1023-byte UTF-8 alternate text, plus aggregate model and codec budgets.
TikuWrite's alternate-text field supports 255 bytes; it refuses longer input
without truncating existing metadata. The desktop's typed clipboard carries
pictures between TikuWrite processes, with `[Picture]` in the fallback text.
Legacy hosts refuse picture Copy/Cut; text export uses alternate-text labels.
PDF output remains untagged, not PDF/UA.

## Tables and nested cells

The immutable hierarchy contains paragraph, table and cell nodes in depth-first
order. Paragraphs also have a flat traversal index for existing text queries.
Node identities are stable document-local IDs, and parent IDs identify the
main body, table or cell. Import validates ownership, row-major cell counts,
unique ordered paragraph references, nonempty cells and bounded nesting before
replacing the document. Paragraph-only imports need no structural array.

Table insertion/removal are single-operation transactions. Insertion occurs
after a paragraph, creates empty cells and a following paragraph, and selects
the first cell in its originating session. Undo restores the preinsert caret.
Deletion removes the full subtree; if its owner would become empty, an empty
paragraph is created in the same transaction. Snapshots retain old structure;
failed edits do not consume IDs or publish partial trees.

Splits, joins and rich paste preserve paragraph ownership. Replacement and
fragment operations spanning different cells or crossing a table are refused.
Formatting can address paragraph text without changing the tree. Backspace at
a cell start cannot join its content to another cell. Tab/Shift+Tab navigate
cell content without inserting literal tabs there. TikuWrite preflights Cut
before clipboard publication and provides Insert table and Delete table.

Layout uses proportional columns, nested tables and rows sized to their
tallest cell. All-zero column weights give equal widths.
A bounded preliminary scan propagates padding, picture and nested-table minimum
widths. Narrow viewports produce overflow warnings rather than collapsed cells.
Line order follows document traversal, not monotonic vertical coordinates;
hit testing and vertical movement use cell geometry. Screen, preview and PDF
use the same retained grid rectangles and paragraph layout.

Pagination keeps tables whole when they fit on an empty page, moving them to
a new page when necessary. Taller tables continue between complete rows and
repeat their configured leading headers. Tall rows use independently advanced
cell fragments, including nested tables. Tables exceeding printable width,
or lines/header groups that cannot fit with enclosing padding, return LIMIT.
Page breaks inside cells return UNSUPPORTED. No partial page count is
published on failure.
The native `tables` required feature stores checked file-local structure
indices; unsupported readers must reject it. Text-only export flattens cell
paragraphs and is not a lossless table format.

`test_richdoc_tables` checks structure, malformed imports, nested insertion,
deletion, only-table replacement, within-cell paste, snapshots, anchors,
undo/redo, navigation, minimum widths, pagination and split/join allocation
refusals. The general command fault suite includes both table operations.
`test_richtables` checks exact native structure, required-feature refusal,
malformed ownership, preview pixels and PDF raster output. Native fault tests
include both a table and a picture. `test_write` and `writetables.script` cover
the application workflows; the script runs the separate application process
with offscreen drawing, not native-host accessibility or IME qualification.

Limits: 256 rows, 64 columns, 4096 cells per table, eight table nesting levels,
65536 structural nodes, and the existing model/history/codec budgets. Cell
merge/split is not implemented. Tables do not complete
milestone M5.

### Rectangular cell selection

`tiku_rich_cells_t` describes direct cells of one table using zero-based
row/column origin and positive dimensions. Validation uses subtraction-based
bounds checks. Snapshot queries resolve same-table cell endpoints and enumerate
rectangle members without allocating or retaining a live document.

CELLS_SET shades a rectangle through one copy-on-write structure allocation,
without changing nested-cell backgrounds or paragraph positions. The clear
helper constructs one checked transaction of paragraph replacements. It
removes text and inline media but retains paragraphs, properties and nested
grids. It refuses more than 1024 nonempty paragraphs before publishing edits.
No-ops, history, old snapshots, anchor mapping, read-only state and generation
checks use the existing model contracts.

Rich View owns the rectangle and validates it against document generation and
session anchor revisions. Bulk edits retain the initiating view's rectangle;
other views invalidate theirs. Other content changes and text selection
movement invalidate it. Resize/scroll preserve the selection; reflow, wheel
and deactivation end pointer capture. Alt/Option-drag starts a rectangle and
Shift-click extends it within the same table. Escape clears it. Delete and
Backspace clear contents; text input and formatting shortcuts are refused.

Cell selections retain separate anchor and moving-end cell IDs. Shift+arrows
move the endpoint without row wrapping; Shift+Home/End reaches row edges,
and Ctrl/Command+Shift+Home/End reaches table corners. Reversing direction
can cross the anchor. Programmatic rectangles start at the top-left and end
at the bottom-right. Bulk shading/clearing preserve these endpoints.
Keyboard extension requires current layout and reveals the moving cell
vertically without changing document content or undo history. Read-only
views permit it; pending layout and busy transactions preserve selection.

Hosts call `tiku_richview_tick()` with nonnegative monotonic microseconds for
stationary-pointer cell-drag scrolling. The first tick sets the clock;
backward time restarts it and each elapsed step is capped at 100 ms.
Velocity is bounded in viewport pixels per second, converted using zoom.
Hit testing clamps vertical coordinates to the viewport and selects the
nearest direct cell of the original table; nested tables do not capture an
outer selection. Scrolling stops at the table extent, primary release,
key input, wheel input, deactivation or geometry invalidation. Invalidated
session anchors cancel the drag before scrolling. OK signals a repaint;
idle ticks return NO_CHANGE. No internal timer, thread or horizontal scroll
is added. TikuWrite supplies host ticks and routes captured moves/releases
before other controls, including release over the toolbar.

Selection-aware view query/execute wrappers interpret CELL_SET and DELETE as
bulk operations, REPLACE_TEXT as TSV paste, and permit history commands.
Other typed commands are disabled
for cell rectangles. Direct session commands remain text-selection APIs.
The shared painter highlights one rectangle per selected row, without nested
double tinting. Selection is not persisted or included in preview/PDF output.

Text export uses quoted tab-separated fields with doubled embedded quotes.
Paragraph breaks remain within fields. Measurement validates the entire
selection before writing the caller's buffer. Pictures and nested tables
return UNSUPPORTED; short buffers remain untouched. This is not a structured
clipboard format. TikuWrite also supplies a rich cell representation through
typed clipboard messages. Captured rectangle identity and document generation
are checked for delayed clipboard replies and shading panels.

Tabular paste accepts quoted or unquoted UTF-8 fields, doubled quotes,
embedded quoted paragraph breaks, empty fields and LF/CRLF/CR row endings.
One final row ending is ignored. Text after a closing quote, unterminated
quotes, unequal row widths, NUL, invalid UTF-8 and object placeholders are
refused. Bounds are 256 rows, 64 columns and 4096 fields; input and decoded
storage are constrained by the destination allocator's memory budget.

A one-cell target expands to the incoming shape within the existing table;
other targets must match exactly. Nested target tables are refused. Text and
inline pictures in target cells are replaced; cell fills and first-paragraph
defaults remain. New text does not import run formatting or links. A counted
sequence clears old text before joining/splitting and inserting new text, so
bytes reclaimed from later cells can satisfy an earlier cell's insertion.
The existing 1024-primitive transaction ceiling applies. Failures preserve
content, generation, next IDs, selection, history and tracked positions.
The view selects the affected rectangle on success, including a visual-only
change when the model already contains the requested empty cells. This
returns OK for repaint without marking document content dirty. Direct model
calls return NO_CHANGE when their edit sequence is empty.

The TSV path is text-only interchange.
The model publishes once and undo/redo exchange the complete result. The
system clipboard remains application-owned. TikuWrite uses the same Paste
menu and shortcut for ordinary text and selected cell rectangles.

`tiku_richdoc_cells_paste()` accepts an immutable source snapshot and rectangle.
It copies paragraph/run styles, lists, links, owned images and cell shading,
retaining destination table geometry and cell identities. Source and target
may overlap in the same document. One cloned candidate is published once;
ordered flat maps preserve tracked positions across the whole replacement.
The view wrapper checks its session restriction and selects the result.
Rich paste follows the same shape rules, copies nested table trees, and checks
aggregate content, memory, style and history limits. A failed operation does
not consume IDs or discard redo history.

Translation Kit's cell envelope is `TIKURC`, zero byte, version 1, followed by
an eight-byte little-endian XML length and exactly that many native XML bytes.
It uses `application/x-tiku-richcells`, the 16 MiB packet ceiling and the
paragraph codec's independent scratch/model limits. Encoding copies selected
cells into a temporary table; unselected text and styles are excluded.
Decoding requires one table tree between empty body paragraphs. Unknown
versions, extra root-level content, truncation and invalid XML are refused.
The validated model limits combined nesting to eight table levels. Copying
retains direct destination cell IDs and the first paragraph ID; copied nested
containers and subsequent paragraphs get fresh IDs.
The caller owns the returned document. Cancellation is checked at codec
boundaries; no per-cell latency guarantee is claimed.

TikuWrite sends this representation with quoted TSV fallback, replacing
pictures by `[Picture]` and flattening nested paragraphs into separate lines
inside their outer quoted field. Rectangle Cut clears contents only after a matching
desktop acknowledgment and retains the grid. Cell Paste uses the rich payload
when recognized, refusing malformed data without lossy fallback. Changing the
rectangle while waiting prevents delayed Cut/Paste.

`tiku_richdoc_cells_insert()` inserts a copied rectangle at a body or cell text
caret. The candidate splits the destination paragraph, inserts a table
between the halves, imports cells and prepares one separator-inclusive map.
No intermediate edit, ID consumption or history entry is published on failure.
The view wrapper refuses nonempty text selections, cell selections, protected
views and exhausted selection revisions, and selects the new grid on success.
TikuWrite uses this path for a rich cell envelope when no rectangle is selected.
Nested source tables and cell-local text carets are supported within the
combined depth limit. New tables retain source padding, border, selected
column weights and headers. Fixed width is multiplied by the selected
columns' fraction, with a one-point minimum; automatic width stays automatic.
Selected header rows are clipped to the source selection. The cell-envelope
encoder uses this same insertion path, so geometry survives process transfer.

Insertion tests cover four UTF-8 caret boundaries, preserved prefix/suffix
formatting, pictures, old table retention, anchor affinity, exact undo/redo,
invalid byte boundaries, cell-local insertion and allocation failures.
Application tests include nonempty-selection refusal and mid-paragraph Paste.
`writecellinsert.script` closes the copying process, inserts into a blank
document in another process and checks one-step undo/redo and redraw output.

`test_richcells_rich` covers 60 placements, style conflicts, empty formatted
cells, links/images, tracked positions, overlap, history, view guards and
allocation refusals. `test_richcellclip` covers selective encoding, resource
ownership, empty styles, all packet truncations, version/length checks, limits
and cancellation. `test_richcells_nested` independently validates copied
preorder hierarchies, unique identities, parent links, media, exact undo/redo,
eight-level success and nine-level refusal. The instrumented native codec suite adds cell-envelope
allocation refusals. `test_write` checks rich rectangle Cut/Paste and stale
replies; `writecellpaste.script` also closes the copying process before paste.

`test_richcells` checks 144 endpoint pairs, independent shading/geometry
oracles, 4096-cell fills, clear limits, snapshot and anchor ownership, quoted
text, nested/media refusal, independent views, input guards and allocation
failures. `test_write` checks menu workflows, rendering, clipboard failures,
clear/undo, stale panels and host-driven cell navigation/scrolling.
`test_richcells_navigation` checks 1152 directed keyboard cases, nested-table
boundaries, read-only and busy guards, time discontinuities, zoom, endpoint
reveal and capture cancellation. `writecells.script` drives the separate process
through menu selection, Alt-drag, fill, deletion, undo and pixel/redraw checks.

### Table continuation and headers

TABLE properties include `header_rows`, from zero through the table's row
count. TABLE_SET changes it atomically with normal no-op, snapshot, history
and refusal semantics. Inserting inside the header prefix increments the
count; deleting inside it decrements the count. Insertion at the header/body
boundary creates a body row. Column edits preserve the count.

The first header group stays with at least one body row when one exists.
Continuation pages reserve the headers' height before placing body rows.
Tall rows split independently per cell; nested tables use the same resumable
row/cell traversal. Line boxes, including inline pictures, are indivisible.
Fragments repeat padding and closed borders; finished cells retain empty
boxes alongside continuing neighbours. Split rows use dedicated pages, and
following content starts on a fresh page. Pagination advances one grid, line
or bounded frame operation per work unit. Array growth can copy retained
records; work units are not wall-clock latency guarantees. Row scratch,
source-grid cursors, the 24-frame/eight-table stack and output pieces are
charged to the document owner and released with the pagination object.

Ordinary ranges and tall-row TEXT/RANGE pieces cover body lines exactly once.
Tall-row pages have zero line_count and nonzero piece_count, queried through
tiku_richpages_piece. CELL pieces provide relocated geometry; TEXT pieces omit
original cell boxes; RANGE pieces include original boxes only at/under their
minimum depth. Ordered painting preserves nested backgrounds. All piece
coordinates are printable-area relative. Optional header ranges
reference earlier lines with a separate source origin. The main origin already
accounts for header height. Preview and PDF render ranges and pieces; PDF resolves
paragraph styles and runs by each line's retained snapshot index, not a
forward-only text cursor. Repeated text, pictures and links retain their
appearance and annotations. Repeated output counts toward PDF output budgets.

Native files require `table-headers` and `tables` when the count is nonzero.
One `header rows="N"` child follows any column children; headers work with
equal or weighted columns. Missing declarations, invalid counts, duplicate
headers and children on non-table nodes are refused. TikuWrite's Repeat
header rows panel accepts whole numbers only and uses the same stale-target
checks as its other table-property panels.

`test_richdoc_pagination` checks 300 independently computed row/header layouts,
bounded/full equivalence, page limits, nested and successive tables, oversized
lines, allocation failures, cancellation, retained ownership and header edits.
`test_richcontinuation` checks exact native round trips, malformed metadata,
preview pixels and independently decoded PDF pages, styled text, images and
links. `test_write` covers valid/invalid header fields and save/reopen;
`writecontinuation.script` checks the separate-process menu, undo and repeated
header pixels while navigating preview pages.

`test_richrowpages` adds 300 independent unequal-column packing oracles,
eight nesting levels with/without headers, exact source-line coverage,
bounded/full equivalence, page limits, retained lifetime, allocation refusal
and cancellation through fragment construction. `test_richrowoutput` checks
preview cell fills/borders, decoded PDF text coverage, image area, hyperlink
bounds and export cancellation. `writerows.script` exercises the independent
TikuWrite process and continuation-page navigation. Fragmentation changes no
model property or native schema; existing native tables use it automatically.

### Table appearance and relative widths

`TABLE_SET` copies validated properties into a new immutable hierarchy,
without changing dimensions, content, identities or positions. Unchanged
properties return NO_CHANGE. Invalid targets, changed dimensions, stale
generations, read-only state and resource failures leave the published model
and history unchanged. The typed command requires an empty caret in a cell;
`tiku_richsnapshot_table` resolves its immediate table and zero-based column.

The 64-entry weight array is all zero for equal columns, or has one finite
0.001..1000 weight per active column and zero in unused entries. Shares are
weights divided by their sum. Insertion retains surviving weights and adds
their mean; deletion removes the corresponding weight. Row edits retain them.
The minimum-width scan divides each cell's content-plus-inset requirement by
that column's share, taking the largest requirement for the table. Nested
tables and pictures therefore cannot collapse a narrow column's content box.

Native documents with explicit weights require `table-columns` and `tables`.
Ordered `column` children hold exact binary32 weights. Missing/extra weights,
invalid numbers and undeclared extensions are refused. Existing equal-column
documents retain their original schema. Shared grid rectangles drive editing,
preview and PDF borders without separate output-width calculations.

TikuWrite exposes size/padding, borders, current-column percentage and equal
widths in its Table menu. Property panels capture the table ID, column and
generation, refusing stale applications. Preview disables these edits.

`test_richdoc_table_props` covers validation, transactional rollback, no-ops,
stable identities, history, grid-weight changes, 200 independent layout cases,
hit tests and nested-table/picture minimum widths. Command fault injection
includes TABLE_SET. Native tests cover exact weighted round trips, malformed
metadata and matching border positions in preview and independently decoded
PDF. App tests cover property controls, resize, stale targets and undo.

### Cell shading and column dragging

CELL records carry straight-alpha ARGB `background`; non-cell backgrounds
are invalid. `CELL_SET` changes one cell through copy-on-write structure,
without position maps. Typed dispatch requires a collapsed caret in the cell.
Snapshot lookup accepts a paragraph ID, not a table or cell ID. No-op, history,
read-only, stale and allocation-failure semantics match TABLE_SET. Surviving
cells retain shading through row/column edits; newly inserted cells are clear.

Native files require `cell-shading` and `tables` for nonzero backgrounds.
One `fill color="..."` child stores unsigned decimal ARGB on a CELL node.
Undeclared fills, extra children and malformed colours are refused. Rendering
paints cell fills before borders and content, including borderless cells;
alpha blends against the existing page or enclosing cell background.

Layout divider hits select the deepest matching table, then the nearest edge.
Bounds combine the adjacent columns' content minima across every row with
padding, borders and representable weights. Rich View uses a four-pixel hit
zone, preserving pointer offset at capture. The guide changes on movement;
only release applies TABLE_SET, preserving the adjacent pair's total weight
and other columns. Unmoved release creates no history. Escape, deactivation,
reflow or scrolling cancels; stale generation/selection and read-only mode
prevent commit. A resource refusal clears the guide without modifying widths.
Hosts must deliver captured pointer events outside the content area too.

TikuWrite exposes fill/clear commands and divider hover cursors. Its fill panel
captures cell ID and generation, so moving to the same column in another row
cannot silently retarget Apply. Preview is read-only.

`test_richdoc_cell_drag` checks 100 zoom/scroll/drag oracles, history, no-op,
pointer offset, cancellation, stale selection, read-only/resource refusal,
nested hits and picture/nested-table minimum widths. CELL_SET participates in
command fault injection. Native tests compare opaque and alpha fills in preview
and decoded PDF, including borderless cells and malformed extension metadata.
TikuWrite tests cover fields, target changes, clear/undo, save/reopen and actual
mouse-event routing; the desktop table script checks guide/fill pixels and undo.
An application regression test keeps the caret above the visible area and
releases the divider over the toolbar: the width changes, without activating
a toolbar control or scrolling back to that caret.

### Row and column changes

`TABLE_GRID` inserts or removes one row/column as a single-edit transaction.
An insertion index may equal the current axis count; deletion requires an
existing index and at least one surviving row/column. Dimension, node,
paragraph, allocation, history and identity limits are checked before
publication. Copied subtrees retain stable cell and paragraph IDs; new cells
have fresh IDs and one empty paragraph. Removal includes nested content.

The six caret-relative session commands resolve the immediate containing
table. They select the inserted cell or nearest surviving coordinate on
deletion. Other surviving paragraph positions keep their byte offsets.
Positions in deleted cells move to a surviving cell; stored history bindings
restore their exact original positions. Anchors created after a grid edit
also map to valid positions when the edit is undone.

`test_richdoc_grid` compares 2000 generated row/column edits and undo/redo
against an independent matrix, including appends, first/last-axis edits,
stable identities and retained content. It also tests nested-cell removal,
snapshot lifetime, late anchors, session selection, invalid indices,
last-axis refusal and ID/dimension exhaustion. The command fault suite
injects allocation failures into all six commands. Native round-trip and
PDF tests include a table after column insertion and row deletion.

TikuWrite publishes a Table menu within the existing six-menu transport.
Styles are grouped under Format / Paragraph styles. Its entry builder and
style shortcut list respect the 28-entry capacity. Tests populate 40 named
styles and exercise row/column editing, undo and redraw. Page preview disables
the grid commands and their action handlers refuse edits in that mode.
