# TikuDoc and TikuSlides: reading office documents

*A plan for showing the word-processor documents and slide decks that
arrive beside datasheets: .docx and .odt in TikuDoc, .pptx in TikuSlides.
2026-09-30.*

## 1. What arrives, and what reading it needs

| Format | Inside | Needs | Have |
|---|---|---|---|
| `.docx` | ZIP of XML parts: `word/document.xml`, styles, numbering, headers, footers, notes, media | XML, OPC relationships, four-level style inheritance, numbering, page layout | ZIP, DEFLATE, PNG, JPEG, TrueType |
| `.odt` | ZIP: `content.xml`, `styles.xml`, `Pictures/` | XML, automatic and common styles, master pages, lists | the same |
| `.pptx` | ZIP: slides, layouts, masters, a theme, media | XML, placeholder and theme inheritance, DrawingML shapes and text frames | the same |
| `.doc`, `.ppt` | OLE2 compound files of binary records | a second reader of everything | not planned |

Nothing in the kits reads XML yet, so a parser comes first; everything
else is layout and drawing.

## 2. Shape

**Translate, then show.**  A document is laid out into pages, and the
pages are written as a PDF in memory; the PDF view kit shows that.  The
view already scrolls, zooms, turns, finds, selects, copies and follows
links on workers, so neither app writes a viewer, and *Export as PDF* is
the same translation written to a file.  PDF's imaging model holds
everything these formats draw -- text in embedded faces, fills, strokes,
gradients, transparency, clipped and turned pictures -- so nothing is
lost on the way but blur.  `kits/translation` is where a format becomes
another; the name was waiting for this.

    .docx, .odt  -->  document model  -->  pages  --\
                                                     >  PDF writer  -->  tiku_pdf_view
    .pptx        -->  slides, shapes, text frames --/

**New kit parts** (`kits/translation` unless noted):

- `tiku_xml` -- XML into a tree, namespaces mapped to fixed prefixes
  (`w:`, `a:`, `p:`, `text:` ...) so readers match names, not URIs.  No
  DTD is read and no entity but the five and character references is
  expanded: no billion laughs, no external files.  Depth is bounded.
- `tiku_archive_read` / `_find` -- one entry into memory, capped and its
  CRC checked.
- `tiku_fontset` -- the machine's faces by family, weight and slant:
  the desktop's fonts folder, the system's, and Office's own where it is
  installed; metric-compatible stand-ins (Calibri → Carlito, Arial →
  Liberation Sans ...), then a face of the same class, then synthetic bold
  and slant; a face per script for characters the chosen one lacks.
- `tiku_pdf_writer` -- pages, paths, text in TrueType subsets (glyph ids
  kept, ToUnicode written so text is found and copied), pictures (JPEG
  passed through, the rest compressed with alpha), links, an outline.
- `tiku_office*` -- the readers, a shared paragraph layout (breaks,
  justification, tabs with leaders, bullets and numbers, inline
  pictures), the page layout for documents (sections, columns, headers
  and footers, footnotes, tables, floating pictures) and the slide
  renderer (placeholders, preset and custom geometry, theme colours and
  fonts, autofit).  One public call: a file in, PDF bytes out.
- `tiku_pdf_view_open_as` -- the view opens a file through a translator.

**Apps.**  TikuDoc and TikuSlides are separate programs, each a window
around the view with a toolbar, an outline column (headings; slide
titles), Find, and Export as PDF.  TikuSlides also plays a deck: one
slide at a time, fitted, on black.

**Hostile files.**  Parts are read with ceilings (64 MB of XML, 32 MB
a picture, 5000 pages), XML depth is bounded, relationship targets are
resolved inside the package only, external targets are never fetched,
and everything runs on a worker in the app's own process.

## 3. Fidelity: what is and is not drawn

Drawn: text in the document's fonts or metric-compatible stand-ins;
paragraph and character styles; lists; tabs and leaders; tables with
merged cells, borders and shading; pictures inline and floating;
headers, footers, page numbers, footnotes; sections with their page
sizes and columns; hyperlinks and an outline from headings; equations
in two dimensions.  In slides:
backgrounds, the ~40 most used preset shapes and any custom geometry,
solid and gradient fills, lines with dashes and arrowheads, pictures
cropped and turned, tables, groups, text frames with insets, anchoring
and autofit.

Shown as a labelled box: surface charts, Office 2016's newer chart
kinds without the picture Office saves beside them, SmartArt without
its drawing, pictures drawn only in EMF+, LibreOffice's own metafiles
(SVM).  Not attempted: complex-script
shaping (Arabic, Indic), right-to-left reordering, kerning, hyphenation,
tracked-change markup (the final text is shown), comments, macros.

## 4. Milestones

**1 -- The parts.**  `tiku_xml`, archive reads, `tiku_fontset`,
`tiku_pdf_writer`; each tested alone, the writer by reading its output
back with the PDF kit.

**2 -- TikuDoc.**  Paragraph and page layout, the docx and odt readers,
`open_as`, the app, its icon, types, help and a lane.  Fixtures made by
`tests/fixtures/tools/gen_office.py` with Python's `zipfile`.

**3 -- TikuSlides.**  The pptx reader and slide renderer, playing, the
app, its icon, types, help and a lane.

**4 -- Later.**  .odp (the slide renderer with ODF's reader), charts
from their cached values, SmartArt's saved drawings, EMF and WMF.

## 5. Where it stands (2026-10-01)

Milestones 1 to 3 are done; of 4, charts, SmartArt's saved drawings and
Windows metafiles are done, .odp not begun.

- **Kit.**  `tiku_xml`, `tiku_archive_read`, `tiku_fontset`,
  `tiku_pdf_writer` and the `tiku_office*` readers, one entry point
  (`tiku_office_pdf`, `tiku_office_export`).  Word drawings -- groups,
  canvases, shapes with text, styled pictures -- and slides share one
  DrawingML engine (`tiku_office_dml*`), which also recolours pictures
  (duotone, greyscale, washout) and reads theme fills.  SmartArt is
  drawn from the drawing the suite saved with it, in slides and in Word.
  Word's VML WordArt is drawn, so watermarks show.  OpenDocument text is read with
  its master pages, outline numbering, aligned and older list labels,
  frames, notes and spanned tables.
- **Charts.**  DrawingML chart parts, in slides and in Word, grouped
  or not, are drawn from the values they cache (`tiku_office_chart*`):
  bars (clustered, stacked, to 100 %), lines, areas, pies, doughnuts,
  scatters, bubbles, radars, stock high-low lines and up-down bars, and
  combinations on two axes; axes scaled as Excel scales them, number
  formats and dates, data labels and each point's own, legends, titles,
  trendlines with their equations, error bars, data tables, pie and bar
  of pie, date and multi-level category axes, labels with fields and
  boxes, display units, theme overrides, the file's own layouts.  3-D
  kinds are drawn flat; surface charts stay grey boxes, and chartex
  kinds show what Office saved beside them for older readers.
  OpenDocument text's chart objects are drawn by the same code, read
  from the table each object keeps (`tiku_office_chart_odf.c`), in
  LibreOffice's colours, scales and anticlockwise pies.
- **Metafiles.**  EMF and WMF pictures -- and so the pictures Office
  keeps of embedded objects: Equation Editor equations, spreadsheets,
  a slide's OLE frames by their fallback or legacy VML picture -- are
  played through a small GDI (`tiku_office_gdi*`, `_emf.c`, `_wmf.c`)
  into PDF paths, text and images: the window and viewport, world
  transforms, pens, brushes and hatches, paths and clipping, fonts
  with their escapement and advances, DIBs with raster operations.
  EMF+ records are passed over; the GDI ones Office writes beside
  them are drawn, and a file of EMF+ alone is a grey box.
- **Equations.**  Office Math, Word's and PowerPoint's
  (`tiku_office_omml.c`), and the MathML of OpenDocument's formula
  objects (`tiku_office_mathml.c`) are read into one tree and set in
  two dimensions as TeX sets them (`tiku_office_math*`): fractions,
  scripts, roots, large operators with their limits, fences grown to
  what they hold, matrices and equation arrays, accents, bars and
  braces.  The OpenType MATH table of Cambria Math or STIX Two Math,
  where the machine has one, gives the lengths, italic corrections,
  accent attachments, bigger variants and assembled parts; without one,
  TeX's lengths in ems and drawn shapes.  Each equation is set before
  the pages are, as a run of its line, its text found and copied in
  reading order.
- **View.**  `tiku_pdf_view_open_as()` opens a file through a
  translator on a worker; `tiku_pdf_view_single()` shows one page alone,
  which is how slides are presented.
- **Apps.**  TikuDoc (`apps/doc`) and TikuSlides (`apps/slides`) are one
  viewer (`apps/office`) under two names: toolbar, outline column, Find,
  Export as PDF; TikuSlides fits slides whole and presents them.  Hidden
  slides are left out, as PowerPoint's own PDFs leave them.
- **Types.**  OOXML's MIME names outrun Tracker's 48-byte type field, so
  .docx and .pptx are typed `application/x-docx` and `application/x-pptx`;
  .odt keeps its own type.
- **Tests.**  `test_office` (kinds, pages and words, sizes, links,
  outline, title, charts, metafiles, OLE pictures, ODF charts,
  equations, export, hostile files) and `test_office_viewer` (the
  apps without a desktop), both also under AddressSanitizer and UBSan;
  the `office` lane opens all three kinds from Tracker and presents.
  Fixtures come from `tests/fixtures/tools/gen_office.py`.

Still boxes or not attempted, as section 3 says: surface charts,
chartex charts saved without a picture, EMF+ alone, SVM, complex
scripts and right-to-left text (equations among them), tracked-change
markup, comments, .odp.
