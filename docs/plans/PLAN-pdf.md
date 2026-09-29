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
   again.  Thumbnails: a Pages column in the outline's place, each page
   drawn small by the document's worker when the pages in view wait for
   nothing, sixty-four kept; it follows the page read and goes where it
   is clicked.  Composite fonts past Identity-H: CMaps carried and named,
   codes of one to four bytes, usecmap; Identity and Adobe's Unicode
   CMaps known without data, the rest read from poppler-data where the
   machine has it (`tiku_pdf_cmaps` names another folder), with its CID
   tables for text given no ToUnicode.  Text written down the page hangs
   from its vertical origin (W2, DW2) and reads in columns.  A CJK font
   left out is drawn with the machine's faces for its language
   (Hiragino, Songti, Apple SD Gothic; Noto CJK elsewhere), any face of
   a collection.  Poppler reads the fixtures as we do and draws the two
   in a face made here pixel for pixel alike; the corpus's text and
   drawing are unchanged on every page checked.  JBIG2 (`tiku_jbig2`),
   all of T.88 but the extended templates: generic regions of every
   template and MMR, symbol dictionaries and text regions arithmetic and
   Huffman coded (their tables, custom ones, refinement and aggregation),
   refinement regions, halftones, striped pages; a byte filter, so images
   and stencils have it through JBIG2Globals.  Its arithmetic decoder
   reads T.88's own test sequence, Annex H's three pages come out as
   jbig2dec draws them, and jbig2enc's pages of our own paper and a TI
   datasheet as it coded them; streams made here for what no encoder
   writes are read as jbig2dec or poppler reads them.  JPEG 2000
   (`tiku_jpx`), all of Part 1: every progression and its changes,
   tiles and tile-parts, precincts, the code-block styles, regions of
   interest, PPM and PPT, both wavelets and both component transforms;
   JP2's palettes, component maps, channel definitions and sYCC.  An
   image filter: in the dictionary's colour space or the file's own,
   its opacity the alpha under SMaskInData, soft masks too.  ISO
   15444-4's twenty-three class-1 streams decode within their limits,
   the lossless ones exactly; its profile streams and JP2 files come out
   as their references but for ICC colour, which the kit takes by
   component count as it does ICCBased.  A picture too big to hold is
   read without its finest resolutions, as OpenJPEG reduces one.  The
   arithmetic decoder is JBIG2's, now shared (`tiku_mq`).  Knockout
   groups: each thing in one laid on the group's backdrop and put in
   place of what the group put down before it, as far as its shape
   reaches, its opacity apart; a group inside is one thing.  Isolated
   or not, they come out as the standard composes them, anti-aliased
   edges too, where poppler knocks out only some; groups nested past
   four deep are drawn as they are rather than dropped.  The clipboard
   past 4 KB: the desk's holds 16 MB, sized to what it holds; each
   session's copy moves by a stamp, not a comparison each event; the
   socket takes a clipboard as one message, counted apart from the
   bound that keeps a stopped application's queue small, and each
   session hears it when it can; X11 reads a property in pieces and
   sends INCR both ways.  SASLprep
   (`tiku_saslprep`, in the support kit) for AES-256 passwords: mapped,
   Unicode 3.2's NFKC with Corrigendum 4, what may not appear refused,
   the direction checked, from tables the build bakes out of Python's
   frozen Unicode 3.2 and RFC 3454 data.  It gives what Python's
   stringprep gives on every code point and 300,000 strings, but where
   CPython orders marks added after 3.2 by today's classes.  Revisions
   2 to 4 hash a password in PDFDocEncoding, as the standard asks; for
   all of them the bytes as typed are tried next, as some writers take
   them.  Rotation: the render calls take a turn past the page's own,
   and `tiku_pdf_turn_point` maps a text box or a link to the turned
   page and back; turned renders are the unturned page's pixels where
   the turn puts them, every one.  TikuPDF's Rotate Left and Right turn
   the pages drawn so far in place and draw the rest turned; selection,
   search, links and the outline find their places on them.  Where each
   document was left -- page, how far down and across, zoom, turn --
   and the documents read lately are kept in TikuPDFRecent in the
   settings folder, a hundred of them; File > Open Recent lists them
   (the empty window stays blank), Clear Menu forgets.  Certificate encryption
   (Adobe.PubSec, every revision pyHanko writes): the reader's key file
   -- PKCS#12 by its own schemes or PBES2, or PEM -- opens the envelope
   holding the seed, RSA v1.5 or OAEP, in AES, triple DES, DES or RC2.
   Signatures: each one's byte range checked against the CMS or bare RSA
   signature in its gap, by RSA (v1.5, PSS) or ECDSA on the NIST and
   Brainpool curves, timestamp tokens too; TikuPDF shows them in a
   Signatures column.  The support kit gained what that took, each read
   against another implementation: SHA-1 and streaming SHA-2, HMAC,
   PBKDF2, AES-192, DES, triple DES and RC2 (`tiku_crypto`), big numbers,
   RSA and ECDSA (`tiku_pubkey`), DER (`tiku_der`), and certificates,
   key files and CMS (`tiku_cms`).  Python's cryptography, LibreSSL and
   pyHanko made every fixture; poppler's pdfsig agrees on each signature
   it checks.  About 420,000 mutated key files, envelopes, signatures and
   the PDFs holding them ran clean under AddressSanitizer and UBSan,
   after the first such run found a certificate's extensions read past
   a failed step.

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
- **Rotation in the render API** (2026-09-28).  The view's turn is a
  parameter of `tiku_pdf_render` and its two siblings, applied past the
  page's own /Rotate; text and links stay in the page's own points and
  are turned by `tiku_pdf_turn_point`, so the kit owns the one
  convention.
- **TikuPDF keeps its own state** (2026-09-28).  One small file in the
  settings folder (TikuPDFRecent), as TikuReader keeps its bookmarks:
  not extended attributes on the documents, which would change files
  people send on and cannot be written on read-only volumes; not a new
  desktop service, which no second application needs yet.
- **A key file, not the keychain** (2026-09-28).  A document sealed to
  certificates opens with a .p12, .pfx or PEM file chosen like a
  document, its password typed where a PDF's password is: it works the
  same on every host, needs nothing of the shell, and keys stay in files
  the person already has.  RSA keys only; an EC key opens no envelope.
- **Signatures are checked, not trusted** (2026-09-28).  Each is held
  against the certificate it carries and the bytes it covers; whether
  that certificate is who it names is left unasked, and each card says
  so.  Checking a chain needs a list of trusted roots the desk does not
  keep.

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
