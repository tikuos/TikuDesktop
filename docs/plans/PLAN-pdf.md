# A PDF reader for the documents tikuOS lives among

*A plan for a reader of datasheets, reference manuals and our own papers
on TikuDesktop -- not of every PDF ever made.  2026-09-27.*

## 1. What the documents actually use

Eight PDFs of the kind this project reads were opened and their objects
counted: the ST boot-ROM manual (Antenna House), the RP2350 datasheet
(asciidoctor), the MSP430FR5994 datasheet (FrameMaker/iText), the Ambiq
brochure, two papers exported from Chrome (Skia), and two of our own
notes printed from macOS.

| Needs | Seen in | Have |
|---|---|---|
| Cross-reference streams and object streams | RP2350, Ambiq, both Chrome | no |
| FlateDecode, PNG predictors | all | inflate, needs growing output |
| Baseline JPEG (DCTDecode) | ST, TI, Ambiq | `tiku_jpeg` |
| TrueType fonts, simple and CID (Identity-H) | all but TI's Type1 | `tiku_ttf`, by code point only |
| CFF fonts (Type1C, FontFile3) | TI: 56 of 65 fonts | `tiku_cff` |
| Non-embedded standard fonts | TI: 9, macOS notes | substitutes from the fonts folder |
| Type 3 fonts (bullets) | both Chrome papers | the content interpreter, reused |
| Fills, strokes, clipping, transforms | all | paths and coverage, no stroker, no even-odd |
| Images with soft masks (SMask) | RP2350: 56, ST, Ambiq | no |
| Colour: Gray, RGB, CMYK, ICCBased, Indexed | TI, notes, RP2350 | no |
| Axial and radial shadings | TI: 88 | no |
| Tiling patterns | ST: 11 (table fills) | no |
| Constant alpha and blend modes | Chrome, TI, RP2350 | no |
| Encryption, AES-256 revision 6 | Ambiq | AES-128 and SHA-256 in tikukits; no AES-256, SHA-384/512 |
| Outlines and internal links | all manuals: 777, 2949, 6054 links | no |
| ToUnicode for search and copy | all | no |
| CCITT fax | TI: one image | no; draw a box |
| Bare Type 1 fonts (FontFile) | none of the eight; every LaTeX paper | no |

Not seen once: JPEG 2000, JBIG2, transparency groups.  Those stay out
until a document of ours needs one.  Type 1 fonts were not seen either,
but arXiv papers are read here too, and LaTeX embeds nothing else.

Already in the tree and written in-house: DEFLATE, baseline JPEG, PNG,
TrueType and CFF outline readers, an anti-aliased coverage rasteriser,
worker threads for rendering off the loop, and applications as
processes of their own, which is where a parser of untrusted files
belongs.  So the road is half laid: what is missing is the PDF file
itself, the drawing interpreter, and a window.

## 2. Shape

A kit and an app, as pictures are done: `kits/translation/tiku_pdf*`
opens a file and renders a page into pixels, with no display and no
window, so it is tested on the host against files; `apps/pdf` is the
window that pages through it.  The kit keeps the image kit's ceilings
(bytes read, pixels made) and loads objects when a page needs them,
because a datasheet is 1,300 pages and 8 MB.

The fonts folder's readers grow two doors the PDF needs: opening a face
from memory and drawing a glyph by its index, not its code point.  The
coverage rasteriser gains the even-odd rule and a stroker; DEFLATE gains
an output that grows.  Nothing else in the kits changes.

## 3. Milestones

1. **The file.**  Lexer and objects, cross-reference tables and streams,
   object streams, a scan that repairs a broken table, Flate with
   predictors, ASCII85/hex/LZW/run-length (small, and datasheets vary),
   the page tree, and text extraction with ToUnicode.  Proof: every page
   count and a line of text from each of the eight files, on the host.
2. **The page.**  The content interpreter: graphics state and transforms,
   fills (both rules), strokes with joins, caps and dashes, clipping by
   mask; colour spaces above; images from Flate and JPEG with SMask
   alpha; text through TrueType, CFF, substitutes, Type 3, and bare
   Type 1 (eexec and Type 1 charstrings, beside `tiku_cff`).  Proof:
   the eight files rendered to PNG and read by eye, then pinned as
   expected pixels for the four small ones we own.
   *Done 2026-09-27*: everything above, with CMYK and YCCK JPEGs
   (the TI package photographs) taught to `tiku_jpeg` on the way; the
   pinned pixels are a page of our own making (fills, strokes, clips,
   forms, images, a stencil, Type 3 text, rotation, bands) since the
   four small PDFs stay out of the repository.  Shadings and patterns
   paint a flat grey until milestone 4.
3. **The window.**  Continuous pages with zoom (fit width, fit page,
   steps), pages rendered in workers and cached, an outline sidebar,
   links that jump within the document, search and copy over the
   extracted text, and the `.pdf` association so a double-click in
   Tracker opens it.
   *Done 2026-09-27*: TikuPDF (`apps/pdf`), every read of a document
   on one worker at a time (jobs share its key) and the window drawing
   what came back; the kit gained the outline, links with named
   destinations, and each character's box.  The RP2350's 1,380 pages
   open in 64 ms and are searched in about a second, in stretches.
4. **Vendor extras.**  Axial and radial shadings, tiling patterns,
   constant alpha; a grey box for the fax image and any progressive JPEG.
   *Done 2026-09-27*: function, axial and radial shadings as fills and
   by `sh`, coloured and uncoloured tiling patterns (the cell drawn once
   at the device's resolution); TI's shaded pin balls and ST's hatched
   key boxes now look as printed.  Mesh shadings (4 to 7), in none of
   the documents, are one tone so what they fill keeps its place.
5. **Later, when a document needs it.**  Encryption for the Ambiq
   documents (AES-256 R6, with AES-128 and RC4 for older ones),
   transparency groups, forms and other annotations.
   *Done 2026-09-27*: the standard security handler, revisions 2 to 6,
   on MD5, SHA-2, RC4 and AES of the support kit's own; the Ambiq
   brochure opens (8 of 8), and fixtures made here are read by poppler
   too.  TikuPDF asks for a password in its find field and keeps the
   author's no-copy.  All sixteen blend modes, luminosity and alpha soft
   masks, groups composited as one, and every annotation's appearance.

6. **What was left after the five.**  Asked for on 2026-09-27, taken
   roughly in the order a reader would notice each:
   - images of scans: CCITT fax (TI's one image), progressive JPEG,
     JBIG2, JPEG 2000 -- grey boxes until each is read;
   - speed: fonts parsed again for every page drawn, every glyph drawn
     from its outline each time (a third of a page's time, measured);
   - optional content (layers drawn whether on or off), annotations
     without an appearance, form fields;
   - composite fonts past Identity-H: CMaps, vertical writing, CJK
     fonts the file leaves out;
   - mesh shadings (4 to 7) and knockout groups;
   - the window: thumbnails, rotation, the place and the recent files
     remembered, whole-word and case search, copying past 4 KB;
   - the rest of security: SASLprep for AES-256 passwords, certificate
     encryption, signatures shown;
   - hardening: a sanitizer build in `mk/`, the JPEG decoder and the
     font readers fuzzed on their own (the PDF kit already is).

   *Done so far*: CCITT fax, Groups 3 and 4 (`tiku_fax`), as a byte
   filter so images, stencils and masks all have it; read against
   libtiff's encoder in every mode, and TI's ESD mark bit for bit as
   poppler reads it, where a black box stood.  Fonts read once for the
   document (a few dozen kept between pages) and each glyph drawn once
   a page for each size and quarter-pixel place: the RP2350's pages
   draw in 3.6 ms, not 5.5, the MSP430's in 4.9, not 7.3.  Progressive
   JPEG, and components sent in scans of their own: jpegtran's
   rewrites of every JPEG in the corpus decode to the very pixels of
   the baseline they came from, and the decoder is fuzzed alone.
   Layers: the default configuration with its view-time usage states,
   membership by policy and by expression, honoured in drawing and in
   the text read; poppler agrees on every probe but the usage states,
   which it ignores and the standard does not.  Annotations that store
   no appearance drawn as a viewer draws their kind: squares, circles,
   lines and their endings, polygons, ink, highlights and the other
   text markup, free text, notes, stamps.  On the way, round caps and
   joins lost their holes: every piece of a stroke is now wound the
   same way, which ST's and TI's page furniture shows too.  Form fields
   drawn from their values when they store no look, or the form says
   its looks are stale (NeedAppearances): text and choices as DA sets
   them, ticks, dots, captions, MK's ground and border.  Mesh shadings
   (4 to 7) read into triangles and filled blending between their
   corners; four made here match poppler's drawing to two levels in
   255.  The fuzz found a DeviceN of more than four inks reading past
   its initial colour; it holds eight now.  `make check-sanitize`
   builds the readers of files from elsewhere under AddressSanitizer
   and UBSan and runs their suites; its first run found a Type 1 font's
   head read past its buffer.  The JPEG decoder (23,000 files) and the
   font readers (342,000 TrueType, CFF and Type 1 programs, the
   corpus's own mutated) were fuzzed alone and are clean.  TikuPDF's
   Edit menu has Match Case and Whole Words, each starting the search
   again.

Milestones 1 and 2 are where the size is: roughly six thousand lines
between them.  The whole, through milestone 4, is about nine thousand
lines, five to eight weeks of focused work.  Each milestone leaves
something usable: after 1 the text of any datasheet can be searched;
after 2 a page can be seen.

## 4. Decisions taken (2026-09-27)

- **Its own app.**  The Reader lays out reflowable text; a PDF is fixed
  pages, closer to the image viewer.  An app of its own borrows the
  Reader's search and outline habits without bending its document model.
- **LaTeX papers count.**  arXiv papers embed Type 1 fonts, so their
  interpreter is part of milestone 2, not an afterthought.
- **No encryption now.**  It gates the Ambiq documents and nothing else
  seen; it waits in milestone 5 for the day one of those is needed.
- **Host only now, a board maybe later.**  The kits are written so a
  device could draw with them, so the renderer keeps its memory bounded
  and draws a page in bands of rows, which costs little on the host and
  makes a board a port rather than a rewrite.  Nothing is built for a
  board in these milestones.

## 5. What this touches and what it does not

Touches: new `kits/translation/tiku_pdf_*.c` and `apps/pdf`; small
additions to `tiku_ttf`, `tiku_glyphpath`, `tiku_inflate`; the crypto
milestone brings AES-256, SHA-384/512, MD5 and RC4 as files of their own;
`tiku_trk_types.c` learns to open `application/pdf`; host tests under
`tests/kits` with the four small PDFs we own as fixtures, the vendor
documents tried locally by an opt-in path as the hardware tests are.

Does not touch: the Reader, the image viewer, the desktop's document
model, the device namespace, and nothing on a board.  No library is
linked; the kits' rule stands.
