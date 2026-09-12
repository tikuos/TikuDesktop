# Help pages: an HTML document view for the toolkit

A plan for a kit view that renders a documentation dialect of HTML, and for
the Help window TikuDesktop opens with it, so help and documentation are
pages a person reads and clicks through rather than paragraphs compiled
into panels.

Every claim about the tree below was checked on 2026-09-12 against
`df46076` (tracker `main`). Where this plan and the code disagree later,
the code is right.

---

## 0. What the tree already gives, and what it forbids

The executing agent must know these before designing anything, because two
of them rule out the obvious approach.

**The kit boundary is enforced.** `kits/tools/check_kits.py` holds
`ALLOWS`, and `"interface": set()` means the interface kit may include
*nothing* outside itself. `translation` (which holds `tiku_image.h`,
`tiku_jpeg.h`, `tiku_hvif.h`, `tiku_icons.h`) is allowed to include
`interface`, never the other way round. **A document view in the interface
kit therefore cannot decode an image.** It must be handed pixels, or ask
for them through a callback the caller fills in.

**Behaviour and paint are separate files.** `check_kits.py` carries a
`BEHAVIOUR` tuple — `tiku_list`, `tiku_scroll`, `tiku_textview`,
`tiku_text_layout` and the rest — and those files compile on a board with
no display, so none of them may include `tiku_gfx.h`, `tiku_font.h`,
`tiku_ui.h`, `tiku_dl.h`, `tiku_window.h` or `tiku_theme.h`. `make
check-behaviour` compiles them displayless against board headers for
cortex-m33. The new view follows `tiku_list` / `tiku_list_paint`.

**Measurement is already face-indexed and board-safe.** `tiku_measure.h`
answers `tiku_measure_width(face, text)`, `_height`, `_ascent` for
`TIKU_MEASURE_PLAIN`, `_BOLD`, `_MONO` without a font in hand. A layout
engine can run entirely in a BEHAVIOUR file.

**Wrapping already exists.** `tiku_text_wrap_next(face, &text, width, out,
cap, last)` reads one wrapped line and advances the cursor, with no
allocation; `tiku_text_elide` does one line with an ellipsis.
`TIKU_TEXT_LINE_MAX` is 256 bytes of scratch. Words wrap at spaces and
long words break at codepoints.

**There is no italic face.** `tiku_font.h` offers plain, bold and mono
only; families are baked at 10, 12, 14 and 16 px by
`kits/interface/tools/genfont.py`, and the METRIC REFERENCE rule in that
header says all layout is computed against the plain face's metrics.
`<em>` has no weight to render in. §7 decides what it does instead.

**Heading sizes survive the wire.** `tiku_dl_face_t` names PLAIN, BOLD,
MONO and MONO_BOLD, and the header states that any face value past those
is a plain face at that many pixels. A heading drawn through
`tiku_font_at(16)` records as face 16 and arrives at 16 on a board.

**Meaning, not colour.** `tiku_ink_t` in `tiku_gfx.h` names what a run of
text *means* — PLAIN, FAINT, WORD, NUM, STR, REMARK, WRONG, PATH — and
`tiku_ink()` resolves it under the live theme. `tiku_span_t` is a `{len,
ink}` run and `tiku_ui_text_spans()` paints a line as runs, one display
list command each. A link is a new ink, not a blue.

**Scrolling is an object.** `tiku_scroll_t` holds total, visible, value
and step, and defers a range handed to it while a button is held or a
re-layout is in progress. The view uses it rather than an int.

**Narration is free if the painter uses kit primitives.**
`tiku_workspace_narrate()` reads the focused window's display list back as
text, which is what `expect-narrate` in the shell suite asserts. A help
page painted with `tiku_text`/`tiku_ui_*` is readable by the test harness
and by anything else that asks the window what it says.

**There is no help anywhere today.** No `Help` string in
`TikuTracker/strings/en.xml`, no help row in the Deskbar leaf, no viewer.
Documentation lives as `kits/interface/doc/*.md`, `TikuTracker/doc/*.md`
and `apps/*/README.md`, all of it written for developers reading the repo.

---

## 1. The decision: a documentation dialect, not a browser

The goal is help pages. A general HTML engine means CSS cascade, floats,
inline-block, scripting and a network stack, and it would still not be a
browser anyone could use. What help needs is a fixed set of elements whose
appearance the theme owns, so a page written once looks right at every font
size, in both themes, and on a board.

So: **the dialect is the contract, the stylesheet is the theme's, and
anything outside the dialect is skipped rather than approximated.** A page
that uses something unsupported still reads; it just does not get the
effect. This is the same bargain the display list already makes with an op
an older end does not know.

---

## 2. The element set

Block elements: `h1` `h2` `h3`, `p`, `ul` `ol` `li`, `pre`, `blockquote`,
`hr`, `table` `tr` `th` `td`, `img`.

Inline elements: `a` (attribute `href`), `b` `strong`, `i` `em`, `code`,
`br`, `span` (ignored, contents kept).

Attributes read: `href`, `id` (anchor targets), `src` and `alt` on `img`,
`colspan` on `td`. Every other attribute is parsed and dropped.

Entities: `&amp; &lt; &gt; &quot; &apos; &nbsp;` and numeric `&#NN;` /
`&#xNN;` up to U+10FFFF, encoded as UTF-8.

Skipped with their whole contents: `script`, `style`, `head`, comments.

Not supported, deliberately: CSS of any kind, nested tables, `float`,
forms and inputs, frames, embedded media, colours or fonts named by the
page. A page that wants those is an application, and this desktop has a
way to write one.

---

## 3. The shape: two kit files and a window

```
kits/interface/tiku_doc.h/.c         BEHAVIOUR: parse, lay out, hit test
kits/interface/tiku_doc_paint.h/.c   paint one laid-out document
TikuTracker/src/ui/windows/tiku_trk_help_window.c/.h   the Help window
```

`tiku_doc.c` joins the `BEHAVIOUR` tuple in `check_kits.py`. Both files go
in the `INTERFACE` block of `mk/tracker.mk` beside the other kit sources.

The view holds, with fixed caps and a flag for each that fills:

- the source text, copied in (`TIKU_DOC_TEXT_MAX`), so the caller may free
  the file it read;
- blocks (`TIKU_DOC_BLOCKS_MAX`): kind, depth, list index, the run range
  it owns;
- inline runs (`TIKU_DOC_RUNS_MAX`): offset and length into the text, face,
  ink, link index;
- links (`TIKU_DOC_LINKS_MAX`): href offset, and the run range it covers;
- anchors (`TIKU_DOC_ANCHORS_MAX`): id offset, block index;
- laid-out lines (`TIKU_DOC_LINES_MAX`): y, height, the run range, and for
  a link line the link index.

**A cap that fills is said, never silently obeyed.** The view reports
`tiku_doc_truncated()` and the window draws "this page was cut at N
blocks" — the lesson from the VFS Inspector, where a `truncated` flag was
stored and never read, so a 128-row listing looked complete.

---

## 4. Layout

Two passes, and only the second repeats.

**Parse** turns the source into blocks and runs once. It is a tokeniser
over tags and text with a small element stack; it never allocates per
element and never recurses. Unknown tags push and pop like `span`.

**Lay out for a width** walks the blocks and produces the line table:
each block asks `tiku_text_wrap_next` for its runs at the width its
indent leaves, and each line records its y. Re-layout happens when the
width changes, when `tiku_font_size()` changes, or when the document is
replaced — nothing else. Total height goes to `tiku_scroll_set_range`.

Block metrics come from the theme's fixed stylesheet, all of them derived
from the plain face so the METRIC REFERENCE rule holds:

| element | face | above | below |
|---|---|---|---|
| `h1` | plain at `size + 6`, bold | 1.5 line | 0.5 line |
| `h2` | plain at `size + 3`, bold | 1.2 line | 0.4 line |
| `h3` | bold at body size | 1 line | 0.3 line |
| `p` | plain | 0 | 0.8 line |
| `li` | plain, indent 20 px per depth | 0 | 0.2 line |
| `pre` | mono, sunken ground, 8 px pad | 0.5 line | 0.5 line |
| `blockquote` | plain, indent 16 px, rule down the left | 0.5 line | 0.5 line |
| `hr` | — | 0.5 line | 0.5 line |

Tables get min/max column widths from their cells in a first sweep and are
laid out to the wider of min and the share of the available width, no
spanning beyond `colspan`, no nesting. If tables prove to be the expensive
part of P0, they move to P3 and the dialect drops them until then.

**Hit testing is arithmetic on the line table**, and it must not repeat the
mistake `tiku_list_at` had until `8f04b1c`: a point below the last line
that was actually drawn lands on nothing, not on the line that would have
been next.

---

## 5. Links, and who decides what they mean

`tiku_doc_hit(doc, x, y)` returns a `tiku_doc_hit_t`: kind (`NONE`,
`TEXT`, `LINK`), the link index, and the href. **The kit resolves
nothing.** It does not know about files, the namespace or the network, and
a kit that knew would have stopped being a kit.

The Help window owns the policy:

- `#id` — scroll to that anchor in the page now shown, and push history.
- `page.html`, `dir/page.html` — a sibling page, resolved under the help
  root, with `..` and absolute paths refused.
- `tiku:<vfs path>` — hand to the desktop's own open, so a help page can
  link to `/devices/<board>/sys` and land the reader in a window on it.
- `http:` `https:` `mailto:` — not followed. The window says so in its
  status line rather than failing silently.

History is a bounded stack of (page, scroll value) with Back and Forward,
so a reader who followed a link can return to the paragraph, not the top.

---

## 6. Images

The view measures and places an image; it never decodes one. The caller
fills two callbacks:

```c
typedef int  (*tiku_doc_image_size_fn)(void *ctx, const char *src,
                                       int *w, int *h);
typedef void (*tiku_doc_image_draw_fn)(void *ctx, const char *src,
                                       tiku_rect_t where);
```

The size callback runs in the BEHAVIOUR half (it returns two integers), the
draw callback in the painter. A caller that provides neither gets the
`alt` text drawn in the FAINT ink inside a frame, which is also what a
board gets: the display list's `PICTURE` op carries raw pixels, and a help
page is not worth that over a serial line.

TikuDesktop's Help window fills them from the translation kit, which it is
allowed to use because it is an application, not a kit.

---

## 7. What `<em>` looks like with no italic face

Three ways, and the plan takes the first:

1. **Same face, PATH ink, no underline.** Nothing new is baked, the
   distinction survives a board and both themes, and `<em>` stops being a
   second bold.
2. Bake an italic for each family. Four sizes times the families already
   baked, in `genfont.py`; it doubles the font blob, which is already
   163,570 lines of generated header.
3. Synthesise a slant in the rasteriser. Cheap to write, and it looks
   like a slanted upright, which is what it is.

Take (1) now and leave (2) as a decision for whoever wants real italics;
the dialect does not change either way.

---

## 8. Keyboard, and how any of this gets tested

The shell suite drives windows by key wherever it can, because coordinates
written into a script go stale the moment a layout moves — which is what
happened to `switcher.script`, `openwithwin.script` and `selectwin.script`
when the Fonts panel was rebuilt. So the Help window is fully keyboard
driven from the start:

- Tab / Shift-Tab walk the links in the page, and the focused link is
  drawn with the focus ring the toolkit already has;
- Return follows the focused link, Backspace goes back;
- arrows, Page Up/Down, Home/End scroll through `tiku_scroll_t`;
- Cmd-W closes, as it now closes every shell panel.

Proof, in the order it should be written:

- `test_doc` (new, `TikuTracker/tests/`): parse and entity decode, the run
  table for nested inline tags, wrapping at a width, the line table's y
  values, hit testing including the strip below the last line, the link and
  anchor tables, and every cap filling with `truncated` set.
- `test_help_window` (new): layout at every font size the toolkit bakes,
  Tab walking the links, Return following one, Back returning to the
  scroll position it left, and a page whose href is refused.
- `help.script` (new shell lane): open Help from the Deskbar, assert the
  index page's words through `expect-narrate`, Tab to a link, follow it,
  assert the second page's words, go back, and close with Cmd-W.

Each of those assertions must be shown to fail before its code exists.

---

## 9. Where help content lives

A `help/` folder beside `strings/`, found the way the apps folder is found
(`find_apps_dir` in `tiku_trk_desktop_apps.c` climbs from the binary) and
overridable with `$TIKU_HELP` for the test harness, exactly as
`$TIKU_APPS` and `$TIKU_STRINGS` already work.

- `help/index.html` — what the Help row opens.
- `help/<topic>.html` — one page per topic.
- `help/apps/<app-id>.html` — an application's own page, which the global
  bar's Help item opens for the focused application.

The pages are written in the dialect by hand. The existing `doc/*.md` are
developer documents and stay where they are; converting them is not part
of this plan, and a markdown-to-dialect build step should not be written
until there is a second reason to want one.

---

## 10. Phases

**P0 — the document model.** `tiku_doc.h/.c` with parse, layout, hit test,
links, anchors and caps. No painting, no window. `test_doc` covers it.
Ends with `make check-behaviour` green, which proves the file is board-safe.

**P1 — the painter.** `tiku_doc_paint.h/.c`, a `TIKU_INK_LINK` added to
`tiku_ink_t` and to every theme table, and the document drawn in the
toolkit demo so it can be looked at. Ends with a page rendered at all four
font sizes in both themes.

**P2 — the Help window.** Shell-owned, opened like the Device Monitor is
(`tiku_workspace_open` plus `tiku_window_set_handlers`, closed through
`close_window`, owned through a `desktop_help_owns`); navigation policy,
history, keyboard link walking, the status line for a refused href. A
`Help` row in the Deskbar leaf — the leaf budget is 32 rows with the last
two reserved since `b477dbf`, so there is room — and `menu.help` in
`strings/en.xml` and `fr.xml`. `test_help_window` and `help.script`.

**P3 — the content, and per-application help.** `help/index.html` and a
page per subsystem; the global bar gains a Help item for the focused
application; `services->help` lets a running application name its own
page. Tables land here if they were deferred from P0.

**P4 — the board.** A board serves a help page over the display list: text
and headings ride the face ladder already, images degrade to `alt`. Worth
doing only once a board has something to explain.

P0 and P1 are one commit each. P2 is one commit for the window and one for
the Deskbar entry and its strings. P3 is one commit per group of pages.

---

## 11. Traps

- **The kit may not include the translation kit.** Images come in through
  callbacks or not at all. `make check-toolkit` refuses anything else.
- **`tiku_doc.c` must not include a paint header**, or `check-behaviour`
  fails and the board path is gone. The temptation is `tiku_gfx.h` for
  `tiku_rect_t`; that type is in `tiku_geom.h`, which is BEHAVIOUR.
- **`TIKU_TEXT_LINE_MAX` is 256 bytes.** A `pre` line longer than that
  must clip predictably, not overrun the scratch buffer.
- **A cap that fills must be said.** Store no flag the drawing never
  reads.
- **Hit testing must agree with painting.** Every rectangle the painter
  draws comes from the same line table the hit test reads, or the two
  drift apart exactly as `tiku_list_at` drifted from `tiku_list_draw`.
- **The shell suite is load-sensitive.** Run `help.script` on an idle
  machine, and compare a failure against the same lane in a worktree at
  HEAD before believing it is a regression.
