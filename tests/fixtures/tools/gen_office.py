#!/usr/bin/env python3
"""The documents test_office translates, made here by Python's zipfile
from XML written by hand -- never by an office suite -- so every feature
is one the test names: Word's styles, lists, tables, notes, headers,
drawings and watermark; OpenDocument's; PowerPoint's masters, layouts,
placeholders, tables and pictures.  And hostile ones.

    python3 tests/fixtures/tools/gen_office.py tests/fixtures/office.inc [dir]

Each document is base64 beside what must come of it: the pages it lays
out to (0: it must be refused) and words its first page must show, a
line each.  With a directory, the documents are written there too, to
be looked at.  Fixed times and no randomness: a run writes the same file.
SPDX-License-Identifier: Apache-2.0
"""
import base64, io, os, struct, sys, zipfile, zlib

OUT = sys.argv[1]
DIR = sys.argv[2] if len(sys.argv) > 2 else None
DOS = (2024, 5, 1, 12, 34, 56)

W = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
     'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/'
     'relationships" '
     'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/'
     'wordprocessingDrawing" '
     'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
     'xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture" '
     'xmlns:wps="http://schemas.microsoft.com/office/word/2010/'
     'wordprocessingShape" '
     'xmlns:wpg="http://schemas.microsoft.com/office/word/2010/'
     'wordprocessingGroup" '
     'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
     'xmlns:v="urn:schemas-microsoft-com:vml" '
     'xmlns:o="urn:schemas-microsoft-com:office:office" '
     'xmlns:w10="urn:schemas-microsoft-com:office:word"')
P = ('xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
     'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
     'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/'
     'relationships"')
O = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
     'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
     'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
     'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
     'xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" '
     'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
     'xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" '
     'xmlns:xlink="http://www.w3.org/1999/xlink" '
     'xmlns:dc="http://purl.org/dc/elements/1.1/" '
     'xmlns:meta="urn:oasis:names:tc:opendocument:xmlns:meta:1.0" '
     'office:version="1.3"')
REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
PKG = "http://schemas.openxmlformats.org/package/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
WML = "application/vnd.openxmlformats-officedocument.wordprocessingml."
PML = "application/vnd.openxmlformats-officedocument.presentationml."
XML = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'


def png(w, h, rgb):
    """A @p w by @p h PNG of one colour, a darker cross through it."""
    rows = b""
    for y in range(h):
        row = b"\x00"
        for x in range(w):
            dark = x == w // 2 or y == h // 2
            row += bytes(c // 2 if dark else c for c in rgb)
        rows += row
    def chunk(t, d):
        return (struct.pack(">I", len(d)) + t + d +
                struct.pack(">I", zlib.crc32(t + d) & 0xffffffff))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def package(parts, first=None):
    """The parts as a ZIP, fixed times, @p first stored first if given."""
    buf = io.BytesIO()
    z = zipfile.ZipFile(buf, "w")
    names = ([first] if first else []) + sorted(p for p in parts
                                                if p != first)
    for name in names:
        data = parts[name]
        data = data.encode("utf-8") if isinstance(data, str) else data
        info = zipfile.ZipInfo(name, DOS)
        info.compress_type = (zipfile.ZIP_STORED if name == "mimetype"
                              else zipfile.ZIP_DEFLATED)
        z.writestr(info, data)
    z.close()
    return buf.getvalue()


def rels(items):
    """A relationships part: (id, type, target[, external])."""
    out = [XML, '<Relationships xmlns="%s">' % PKG]
    for it in items:
        ext = ' TargetMode="External"' if len(it) > 3 and it[3] else ""
        out.append('<Relationship Id="%s" Type="%s%s" Target="%s"%s/>'
                   % (it[0], REL if "://" not in it[1] else "", it[1],
                      it[2], ext))
    out.append("</Relationships>")
    return "".join(out)


def types(overrides, defaults=(("rels", "application/vnd.openxmlformats-"
                                "package.relationships+xml"),
                               ("xml", "application/xml"),
                               ("png", "image/png"))):
    out = [XML, '<Types xmlns="%s">' % CT]
    out += ['<Default Extension="%s" ContentType="%s"/>' % d
            for d in defaults]
    out += ['<Override PartName="/%s" ContentType="%s"/>' % o
            for o in overrides]
    out.append("</Types>")
    return "".join(out)


class Doc:
    """A document, what it must lay out to, and why it is here."""
    def __init__(self, name, ext, doc, data, pages, words):
        self.name, self.ext, self.doc = name, ext, doc
        self.data, self.pages, self.words = data, pages, words


DOCS = []

# ------------------------------------------------------------------- Word

THEME = (XML + '<a:theme xmlns:a="http://schemas.openxmlformats.org/'
         'drawingml/2006/main" name="Office"><a:themeElements>'
         '<a:clrScheme name="Office"><a:dk1><a:sysClr val="windowText" '
         'lastClr="000000"/></a:dk1><a:lt1><a:sysClr val="window" '
         'lastClr="FFFFFF"/></a:lt1><a:dk2><a:srgbClr val="44546A"/></a:dk2>'
         '<a:lt2><a:srgbClr val="E7E6E6"/></a:lt2><a:accent1><a:srgbClr '
         'val="4472C4"/></a:accent1><a:accent2><a:srgbClr val="ED7D31"/>'
         '</a:accent2><a:accent3><a:srgbClr val="A5A5A5"/></a:accent3>'
         '<a:accent4><a:srgbClr val="FFC000"/></a:accent4><a:accent5>'
         '<a:srgbClr val="5B9BD5"/></a:accent5><a:accent6><a:srgbClr '
         'val="70AD47"/></a:accent6><a:hlink><a:srgbClr val="0563C1"/>'
         '</a:hlink><a:folHlink><a:srgbClr val="954F72"/></a:folHlink>'
         '</a:clrScheme><a:fontScheme name="Office"><a:majorFont><a:latin '
         'typeface="Calibri Light"/><a:ea typeface=""/><a:cs typeface=""/>'
         '</a:majorFont><a:minorFont><a:latin typeface="Calibri"/><a:ea '
         'typeface=""/><a:cs typeface=""/></a:minorFont></a:fontScheme>'
         '<a:fmtScheme name="Office"><a:fillStyleLst><a:solidFill>'
         '<a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr '
         'val="phClr"><a:tint val="50000"/></a:schemeClr></a:solidFill>'
         '<a:solidFill><a:schemeClr val="phClr"><a:shade val="80000"/>'
         '</a:schemeClr></a:solidFill></a:fillStyleLst><a:lnStyleLst><a:ln '
         'w="6350"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill>'
         '</a:ln><a:ln w="12700"><a:solidFill><a:schemeClr val="phClr"/>'
         '</a:solidFill></a:ln><a:ln w="19050"><a:solidFill><a:schemeClr '
         'val="phClr"/></a:solidFill></a:ln></a:lnStyleLst>'
         '<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle>'
         '<a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle>'
         '<a:effectLst/></a:effectStyle></a:effectStyleLst>'
         '<a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/>'
         '</a:solidFill><a:solidFill><a:schemeClr val="phClr"><a:tint '
         'val="95000"/></a:schemeClr></a:solidFill><a:solidFill>'
         '<a:schemeClr val="phClr"><a:shade val="90000"/></a:schemeClr>'
         '</a:solidFill></a:bgFillStyleLst></a:fmtScheme></a:themeElements>'
         '</a:theme>')

DSP = ('xmlns:dsp="http://schemas.microsoft.com/office/drawing/2008/diagram" '
       'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"')
DIAGRAM_DATA = (XML + '<dgm:dataModel xmlns:dgm="http://schemas.openxmlformats'
                '.org/drawingml/2006/diagram" %s><dgm:ptLst/><dgm:cxnLst/>'
                '<dgm:extLst><a:ext uri="http://schemas.microsoft.com/office/'
                'drawing/2008/diagram"><dsp:dataModelExt relId="rId6"/>'
                '</a:ext></dgm:extLst></dgm:dataModel>' % DSP)


def dsp_box(n, x, prst, fill, text, k=1.0):
    """A shape of a diagram's saved drawing: its place, geometry, fill,
    style and words, all @p k times their slide size."""
    return ('<dsp:sp modelId="{%d}"><dsp:nvSpPr><dsp:cNvPr id="%d" name=""/>'
            '<dsp:cNvSpPr/></dsp:nvSpPr><dsp:spPr><a:xfrm><a:off x="%d" '
            'y="%d"/><a:ext cx="%d" cy="%d"/></a:xfrm>'
            '<a:prstGeom prst="%s"><a:avLst/></a:prstGeom><a:solidFill>'
            '<a:schemeClr val="%s"/></a:solidFill></dsp:spPr><dsp:style>'
            '<a:lnRef idx="0"><a:scrgbClr r="0" g="0" b="0"/></a:lnRef>'
            '<a:fillRef idx="1"><a:scrgbClr r="0" g="0" b="0"/></a:fillRef>'
            '<a:effectRef idx="0"><a:scrgbClr r="0" g="0" b="0"/>'
            '</a:effectRef><a:fontRef idx="minor"><a:schemeClr val="lt1"/>'
            '</a:fontRef></dsp:style><dsp:txBody><a:bodyPr anchor="ctr"/>'
            '<a:lstStyle/><a:p><a:pPr algn="ctr"/><a:r><a:rPr lang="en-GB" '
            'sz="%d"/><a:t>%s</a:t></a:r></a:p></dsp:txBody></dsp:sp>'
            % (n, n, int(x * k), int(228600 * k), int(2743200 * k),
               int(1371600 * k), prst, fill, int(2400 * k), text))


def diagram_drawing(k=1.0):
    """The drawing an office suite saves beside a diagram: two boxes, and
    an arrow between them, @p k times their slide size."""
    return (XML + '<dsp:drawing %s><dsp:spTree><dsp:nvGrpSpPr><dsp:cNvPr '
            'id="0" name=""/><dsp:cNvGrpSpPr/></dsp:nvGrpSpPr><dsp:grpSpPr/>'
            '%s%s%s</dsp:spTree></dsp:drawing>'
            % (DSP, dsp_box(1, 0, "roundRect", "accent1", "Plan", k),
               dsp_box(2, 3200400, "rightArrow", "accent2", "then", k),
               dsp_box(3, 6400800, "roundRect", "accent6", "Build", k)))


W_STYLES = (XML + '<w:styles %s><w:docDefaults><w:rPrDefault><w:rPr>'
            '<w:rFonts w:asciiTheme="minorHAnsi" w:hAnsiTheme="minorHAnsi"/>'
            '<w:sz w:val="22"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr>'
            '<w:spacing w:after="160" w:line="259" w:lineRule="auto"/>'
            '</w:pPr></w:pPrDefault></w:docDefaults>'
            '<w:style w:type="paragraph" w:default="1" w:styleId="Normal">'
            '<w:name w:val="Normal"/></w:style>'
            '<w:style w:type="paragraph" w:styleId="Heading1"><w:name '
            'w:val="heading 1"/><w:basedOn w:val="Normal"/><w:pPr>'
            '<w:keepNext/><w:spacing w:before="240"/><w:outlineLvl '
            'w:val="0"/></w:pPr><w:rPr><w:rFonts w:asciiTheme="majorHAnsi" '
            'w:hAnsiTheme="majorHAnsi"/><w:color w:val="2F5496"/><w:sz '
            'w:val="32"/></w:rPr></w:style>'
            '<w:style w:type="paragraph" w:styleId="ListParagraph"><w:name '
            'w:val="List Paragraph"/><w:basedOn w:val="Normal"/><w:pPr>'
            '<w:ind w:left="720"/><w:contextualSpacing/></w:pPr></w:style>'
            '<w:style w:type="character" w:styleId="Strong"><w:name '
            'w:val="Strong"/><w:rPr><w:b/></w:rPr></w:style>'
            '<w:style w:type="table" w:styleId="Grid"><w:name w:val="Table '
            'Grid"/><w:tblPr><w:tblBorders><w:top w:val="single" w:sz="4"/>'
            '<w:left w:val="single" w:sz="4"/><w:bottom w:val="single" '
            'w:sz="4"/><w:right w:val="single" w:sz="4"/><w:insideH '
            'w:val="single" w:sz="4"/><w:insideV w:val="single" w:sz="4"/>'
            '</w:tblBorders></w:tblPr><w:tblStylePr w:type="firstRow"><w:rPr>'
            '<w:b/></w:rPr><w:tcPr><w:shd w:val="clear" w:fill="D9E2F3"/>'
            '</w:tcPr></w:tblStylePr></w:style>'
            '<w:style w:type="paragraph" w:styleId="FootnoteText"><w:name '
            'w:val="footnote text"/><w:basedOn w:val="Normal"/><w:rPr><w:sz '
            'w:val="20"/></w:rPr></w:style></w:styles>' % W)

W_NUMBERING = (XML + '<w:numbering %s><w:abstractNum w:abstractNumId="0">'
               '<w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt '
               'w:val="decimal"/><w:lvlText w:val="%%1."/><w:pPr><w:ind '
               'w:left="720" w:hanging="360"/></w:pPr></w:lvl><w:lvl '
               'w:ilvl="1"><w:start w:val="1"/><w:numFmt w:val="lowerLetter"'
               '/><w:lvlText w:val="%%2)"/><w:pPr><w:ind w:left="1440" '
               'w:hanging="360"/></w:pPr></w:lvl></w:abstractNum>'
               '<w:abstractNum w:abstractNumId="1"><w:lvl w:ilvl="0"><w:numFmt '
               'w:val="bullet"/><w:lvlText w:val="•"/><w:pPr><w:ind '
               'w:left="720" w:hanging="360"/></w:pPr></w:lvl></w:abstractNum>'
               '<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>'
               '<w:num w:numId="2"><w:abstractNumId w:val="1"/></w:num>'
               '</w:numbering>' % W)


def wp(text, style=None, num=None, lvl=0, runs=None):
    """A Word paragraph: its style, list, and runs (or one of @p text)."""
    ppr = ""
    if style or num:
        ppr = "<w:pPr>%s%s</w:pPr>" % (
            '<w:pStyle w:val="%s"/>' % style if style else "",
            '<w:numPr><w:ilvl w:val="%d"/><w:numId w:val="%d"/></w:numPr>'
            % (lvl, num) if num else "")
    body = runs if runs is not None else '<w:r><w:t xml:space="preserve">%s'\
        '</w:t></w:r>' % text
    return "<w:p>%s%s</w:p>" % (ppr, body)


def wcell(text, span=1, vmerge=None):
    tcpr = ""
    if span > 1 or vmerge:
        tcpr = "<w:tcPr>%s%s</w:tcPr>" % (
            '<w:gridSpan w:val="%d"/>' % span if span > 1 else "",
            '<w:vMerge w:val="restart"/>' if vmerge == "restart" else
            "<w:vMerge/>" if vmerge else "")
    return "<w:tc>%s%s</w:tc>" % (tcpr, wp(text))


W_TABLE = ('<w:tbl><w:tblPr><w:tblStyle w:val="Grid"/><w:tblW w:w="0" '
           'w:type="auto"/><w:tblLook w:val="04A0"/></w:tblPr><w:tblGrid>'
           '<w:gridCol w:w="3000"/><w:gridCol w:w="3000"/><w:gridCol '
           'w:w="3000"/></w:tblGrid>'
           '<w:tr><w:trPr><w:tblHeader/></w:trPr>%s%s%s</w:tr>'
           '<w:tr>%s%s</w:tr><w:tr>%s%s%s</w:tr></w:tbl>'
           % (wcell("Planet"), wcell("Moons"), wcell("Rings"),
              wcell("Saturn spans two", span=2), wcell("Merged",
                                                       vmerge="restart"),
              wcell("Jupiter"), wcell("95"), wcell("", vmerge="cont")))

W_PICTURE = ('<w:r><w:drawing><wp:inline><wp:extent cx="457200" cy="457200"/>'
             '<wp:docPr id="1" name="Dot"/><a:graphic><a:graphicData '
             'uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
             '<pic:pic><pic:nvPicPr><pic:cNvPr id="1" name="dot.png"/>'
             '<pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip '
             'r:embed="rIdImg"/><a:stretch><a:fillRect/></a:stretch>'
             '</pic:blipFill><pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext '
             'cx="457200" cy="457200"/></a:xfrm><a:prstGeom prst="rect">'
             '<a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData>'
             '</a:graphic></wp:inline></w:drawing></w:r>')

W_GROUP = ('<w:r><mc:AlternateContent><mc:Choice Requires="wpg"><w:drawing>'
           '<wp:anchor distT="0" distB="0" distL="114300" distR="114300" '
           'simplePos="0" relativeHeight="1" behindDoc="0" locked="0" '
           'layoutInCell="1" allowOverlap="1"><wp:simplePos x="0" y="0"/>'
           '<wp:positionH relativeFrom="margin"><wp:align>right</wp:align>'
           '</wp:positionH><wp:positionV relativeFrom="paragraph">'
           '<wp:posOffset>0</wp:posOffset></wp:positionV><wp:extent '
           'cx="1828800" cy="914400"/><wp:wrapSquare wrapText="bothSides"/>'
           '<wp:docPr id="2" name="Callout"/><a:graphic><a:graphicData '
           'uri="http://schemas.microsoft.com/office/word/2010/'
           'wordprocessingGroup"><wpg:wgp><wpg:cNvGrpSpPr/><wpg:grpSpPr>'
           '<a:xfrm><a:off x="0" y="0"/><a:ext cx="1828800" cy="914400"/>'
           '<a:chOff x="0" y="0"/><a:chExt cx="1828800" cy="914400"/>'
           '</a:xfrm></wpg:grpSpPr><wps:wsp><wps:cNvPr id="3" name="Box"/>'
           '<wps:cNvSpPr/><wps:spPr><a:xfrm><a:off x="0" y="0"/><a:ext '
           'cx="1828800" cy="914400"/></a:xfrm><a:prstGeom '
           'prst="roundRect"><a:avLst/></a:prstGeom><a:solidFill>'
           '<a:schemeClr val="accent1"/></a:solidFill></wps:spPr><wps:txbx>'
           '<w:txbxContent><w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r>'
           '<w:rPr><w:color w:val="FFFFFF"/></w:rPr><w:t>Group callout</w:t>'
           '</w:r></w:p></w:txbxContent></wps:txbx><wps:bodyPr lIns="91440" '
           'tIns="45720" rIns="91440" bIns="45720" anchor="ctr"/></wps:wsp>'
           '</wpg:wgp></a:graphicData></a:graphic></wp:anchor></w:drawing>'
           '</mc:Choice><mc:Fallback><w:pict/></mc:Fallback>'
           '</mc:AlternateContent></w:r>')

W_DIAGRAM = ('<w:r><w:drawing><wp:inline><wp:extent cx="5486400" '
             'cy="960120"/><wp:docPr id="5" name="Diagram"/><a:graphic>'
             '<a:graphicData uri="http://schemas.openxmlformats.org/'
             'drawingml/2006/diagram"><dgm:relIds xmlns:dgm="http://schemas.'
             'openxmlformats.org/drawingml/2006/diagram" r:dm="rIdDm" '
             'r:lo="rIdLo" r:qs="rIdQs" r:cs="rIdCs"/></a:graphicData>'
             '</a:graphic></wp:inline></w:drawing></w:r>')

W_WATERMARK = ('<w:p><w:r><w:pict><v:shape id="WaterMark" type="#_x0000_t136"'
               ' style="position:absolute;margin-left:0;margin-top:0;'
               'width:400pt;height:100pt;rotation:315;z-index:-1;'
               'mso-position-horizontal:center;mso-position-horizontal-'
               'relative:margin;mso-position-vertical:center;mso-position-'
               'vertical-relative:margin" fillcolor="silver" stroked="f">'
               '<v:fill opacity=".5"/><v:textpath style="font-family:&quot;'
               'Calibri&quot;;font-size:1pt" string="DRAFT"/></v:shape>'
               '</w:pict></w:r></w:p>')


def docx_features():
    note = ('<w:r><w:rPr><w:vertAlign w:val="superscript"/></w:rPr>'
            '<w:footnoteReference w:id="1"/></w:r>')
    body = "".join([
        wp("Word features", "Heading1"),
        wp(None, runs='<w:r><w:t xml:space="preserve">Plain, </w:t></w:r>'
           '<w:r><w:rPr><w:rStyle w:val="Strong"/></w:rPr><w:t>strong</w:t>'
           '</w:r><w:r><w:t xml:space="preserve"> and </w:t></w:r><w:r>'
           '<w:rPr><w:i/><w:color w:val="C00000"/></w:rPr><w:t>red italic'
           '</w:t></w:r><w:r><w:t xml:space="preserve"> text with a note'
           '</w:t></w:r>' + note + '<w:hyperlink r:id="rIdLink"><w:r><w:t '
           'xml:space="preserve"> and a link</w:t></w:r></w:hyperlink>'),
        wp(None, runs=W_GROUP + '<w:r><w:t>A shape group floats beside '
           'this paragraph, its text box set by the pager.</w:t></w:r>'),
        wp("First numbered", "ListParagraph", 1, 0),
        wp("Nested lettered", "ListParagraph", 1, 1),
        wp("Second numbered", "ListParagraph", 1, 0),
        wp("A bullet", "ListParagraph", 2, 0),
        W_TABLE,
        wp(None, runs=W_PICTURE + '<w:r><w:t xml:space="preserve"> an inline '
           'picture</w:t></w:r>'),
        '<w:p><w:r><w:br w:type="page"/></w:r><w:r><w:t>Second page</w:t>'
        '</w:r></w:p>',
        wp(None, runs=W_DIAGRAM),
        '<w:sectPr><w:headerReference w:type="default" r:id="rIdHead"/>'
        '<w:footerReference w:type="default" r:id="rIdFoot"/><w:pgSz '
        'w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" '
        'w:bottom="1440" w:left="1440" w:header="720" w:footer="720" '
        'w:gutter="0"/></w:sectPr>'])
    doc = XML + '<w:document %s><w:body>%s</w:body></w:document>' % (W, body)
    header = (XML + '<w:hdr %s>%s%s</w:hdr>'
              % (W, wp("Header of the features"), W_WATERMARK))
    footer = (XML + '<w:ftr %s>%s</w:ftr>' % (W, wp(None, runs=
              '<w:r><w:t xml:space="preserve">Page </w:t></w:r><w:r>'
              '<w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText>'
              ' PAGE </w:instrText></w:r><w:r><w:fldChar w:fldCharType='
              '"separate"/></w:r><w:r><w:t>1</w:t></w:r><w:r><w:fldChar '
              'w:fldCharType="end"/></w:r><w:r><w:t xml:space="preserve"> of '
              '</w:t></w:r><w:fldSimple w:instr=" NUMPAGES "><w:r><w:t>1'
              '</w:t></w:r></w:fldSimple>')))
    notes = (XML + '<w:footnotes %s><w:footnote w:type="separator" '
             'w:id="-1"><w:p/></w:footnote><w:footnote w:id="1">%s'
             '</w:footnote></w:footnotes>'
             % (W, wp(None, "FootnoteText", runs='<w:r><w:footnoteRef/>'
                      '</w:r><w:r><w:t xml:space="preserve"> The note at the '
                      'foot.</w:t></w:r>')))
    parts = {
        "[Content_Types].xml": types([
            ("word/document.xml", WML + "document.main+xml"),
            ("word/styles.xml", WML + "styles+xml"),
            ("word/numbering.xml", WML + "numbering+xml"),
            ("word/footnotes.xml", WML + "footnotes+xml"),
            ("word/header1.xml", WML + "header+xml"),
            ("word/footer1.xml", WML + "footer+xml"),
            ("word/theme/theme1.xml",
             "application/vnd.openxmlformats-officedocument.theme+xml")]),
        "_rels/.rels": rels([("rId1", "officeDocument",
                              "word/document.xml")]),
        "word/_rels/document.xml.rels": rels([
            ("rIdStyles", "styles", "styles.xml"),
            ("rIdNum", "numbering", "numbering.xml"),
            ("rIdNotes", "footnotes", "footnotes.xml"),
            ("rIdHead", "header", "header1.xml"),
            ("rIdFoot", "footer", "footer1.xml"),
            ("rIdTheme", "theme", "theme/theme1.xml"),
            ("rIdImg", "image", "media/dot.png"),
            ("rIdDm", "diagramData", "diagrams/data1.xml"),
            ("rIdDd", "http://schemas.microsoft.com/office/2007/"
             "relationships/diagramDrawing", "diagrams/drawing1.xml"),
            ("rIdLink", "hyperlink", "https://example.com/", True)]),
        "word/diagrams/data1.xml": DIAGRAM_DATA.replace('"rId6"',
                                                        '"rIdDd"'),
        "word/diagrams/drawing1.xml": diagram_drawing(0.6),
        "word/document.xml": doc, "word/styles.xml": W_STYLES,
        "word/numbering.xml": W_NUMBERING, "word/footnotes.xml": notes,
        "word/header1.xml": header, "word/footer1.xml": footer,
        "word/theme/theme1.xml": THEME,
        "word/media/dot.png": png(8, 8, (40, 120, 200))}
    return package(parts)


DOCS.append(Doc("docx_features", ".docx", "Word: styles, a note, a group, "
                "lists, a table, a picture, header, footer, watermark",
                docx_features(), 2,
                ["Word features", "strong", "red italic", "Group callout",
                 "1.", "a)", "Nested lettered", "Saturn spans two",
                 "Merged", "inline picture", "Page 1 of 2", "DRAFT",
                 "The note at the foot", "Header of the features"]))

# ----------------------------------------------------------- OpenDocument

O_STYLES = (XML + '<office:document-styles %s><office:font-face-decls>'
            '<style:font-face style:name="Liberation Serif" svg:font-family='
            '"&apos;Liberation Serif&apos;"/><style:font-face style:name='
            '"Liberation Sans" svg:font-family="&apos;Liberation Sans&apos;"/>'
            '</office:font-face-decls><office:styles>'
            '<style:default-style style:family="paragraph">'
            '<style:paragraph-properties style:tab-stop-distance="1.25cm"/>'
            '<style:text-properties style:font-name="Liberation Serif" '
            'fo:font-size="12pt"/></style:default-style>'
            '<style:style style:name="Standard" style:family="paragraph"/>'
            '<style:style style:name="Text_20_body" style:family="paragraph" '
            'style:parent-style-name="Standard"><style:paragraph-properties '
            'fo:margin-bottom="0.247cm" fo:line-height="115%"/></style:style>'
            '<style:style style:name="Heading" style:family="paragraph" '
            'style:parent-style-name="Standard"><style:paragraph-properties '
            'fo:margin-top="0.423cm" fo:margin-bottom="0.212cm" '
            'fo:keep-with-next="always"/><style:text-properties '
            'style:font-name="Liberation Sans" fo:font-size="14pt"/>'
            '</style:style><style:style style:name="Heading_20_1" '
            'style:family="paragraph" style:parent-style-name="Heading">'
            '<style:text-properties fo:font-size="130%" fo:font-weight="bold"'
            '/></style:style><style:style style:name="Heading_20_2" '
            'style:family="paragraph" style:parent-style-name="Heading">'
            '<style:text-properties fo:font-size="115%" fo:font-weight="bold"'
            '/></style:style><style:style style:name="Footnote" '
            'style:family="paragraph" style:parent-style-name="Standard">'
            '<style:paragraph-properties fo:margin-left="0.6cm" '
            'fo:text-indent="-0.6cm"/><style:text-properties '
            'fo:font-size="10pt"/></style:style>'
            '<style:style style:name="Header" style:family="paragraph" '
            'style:parent-style-name="Standard"/>'
            '<text:outline-style style:name="Outline"><text:outline-level-'
            'style text:level="1" style:num-format="1" style:num-suffix=" ">'
            '<style:list-level-properties text:list-level-position-and-'
            'space-mode="label-alignment"><style:list-level-label-alignment '
            'text:label-followed-by="nothing"/></style:list-level-properties>'
            '</text:outline-level-style><text:outline-level-style '
            'text:level="2" style:num-format="1" text:display-levels="2" '
            'style:num-suffix=" "><style:list-level-properties text:list-'
            'level-position-and-space-mode="label-alignment"><style:list-'
            'level-label-alignment text:label-followed-by="nothing"/>'
            '</style:list-level-properties></text:outline-level-style>'
            '</text:outline-style></office:styles><office:automatic-styles>'
            '<style:page-layout style:name="pm1"><style:page-layout-'
            'properties fo:page-width="21.001cm" fo:page-height="29.7cm" '
            'fo:margin-top="2cm" fo:margin-bottom="2cm" fo:margin-left="2cm" '
            'fo:margin-right="2cm"/><style:header-style><style:header-footer-'
            'properties fo:min-height="0.6cm" fo:margin-bottom="0.5cm"/>'
            '</style:header-style><style:footer-style><style:header-footer-'
            'properties fo:min-height="0.6cm" fo:margin-top="0.5cm"/>'
            '</style:footer-style></style:page-layout><style:page-layout '
            'style:name="pm2"><style:page-layout-properties fo:page-width='
            '"29.7cm" fo:page-height="21.001cm" style:print-orientation='
            '"landscape" fo:margin-top="2cm" fo:margin-bottom="2cm" '
            'fo:margin-left="2cm" fo:margin-right="2cm"/></style:page-layout>'
            '</office:automatic-styles><office:master-styles>'
            '<style:master-page style:name="Standard" style:page-layout-name='
            '"pm1"><style:header><text:p text:style-name="Header">Header of '
            'the text</text:p></style:header><style:footer><text:p '
            'text:style-name="Header">Page <text:page-number text:select-page'
            '="current">1</text:page-number> of <text:page-count>1'
            '</text:page-count></text:p></style:footer></style:master-page>'
            '<style:master-page style:name="Landscape" style:page-layout-name'
            '="pm2"/></office:master-styles></office:document-styles>'
            ).replace("%s", O, 1)


def odt_features():
    auto = ('<office:automatic-styles>'
            '<style:style style:name="P0" style:family="paragraph" '
            'style:parent-style-name="Heading_20_1" style:master-page-name='
            '"Standard"/><style:style style:name="P1" style:family='
            '"paragraph" style:parent-style-name="Text_20_body"/>'
            '<style:style style:name="P2" style:family='
            '"paragraph" style:parent-style-name="Text_20_body" '
            'style:master-page-name="Landscape"/><style:style '
            'style:name="P3" style:family="paragraph" style:parent-style-'
            'name="Text_20_body"><style:paragraph-properties fo:text-align='
            '"center"/></style:style><style:style style:name="T1" '
            'style:family="text"><style:text-properties fo:font-weight='
            '"bold"/></style:style><style:style style:name="T2" '
            'style:family="text"><style:text-properties fo:font-style='
            '"italic" fo:color="#c9211e"/></style:style><style:style '
            'style:name="Table1" style:family="table"><style:table-'
            'properties style:width="15cm" table:align="left"/>'
            '</style:style><style:style style:name="Table1.A" style:family='
            '"table-column"><style:table-column-properties style:column-'
            'width="5cm"/></style:style><style:style style:name="Table1.A1" '
            'style:family="table-cell"><style:table-cell-properties '
            'fo:padding="0.1cm" fo:border="0.5pt solid #000000" '
            'fo:background-color="#dee6ef"/></style:style><style:style '
            'style:name="Table1.A2" style:family="table-cell"><style:table-'
            'cell-properties fo:padding="0.1cm" fo:border="0.5pt solid '
            '#000000"/></style:style><style:style style:name="fr1" '
            'style:family="graphic"><style:graphic-properties style:wrap='
            '"parallel" style:horizontal-pos="right" style:horizontal-rel='
            '"paragraph" style:vertical-pos="top" style:vertical-rel='
            '"paragraph"/></style:style>'
            '<text:list-style style:name="L1"><text:list-level-style-bullet '
            'text:level="1" text:bullet-char="•"><style:list-level-'
            'properties text:list-level-position-and-space-mode="label-'
            'alignment"><style:list-level-label-alignment text:label-'
            'followed-by="listtab" text:list-tab-stop-position="1.27cm" '
            'fo:text-indent="-0.635cm" fo:margin-left="1.27cm"/>'
            '</style:list-level-properties></text:list-level-style-bullet>'
            '<text:list-level-style-bullet text:level="2" text:bullet-char='
            '"◦"><style:list-level-properties text:list-level-position-'
            'and-space-mode="label-alignment"><style:list-level-label-'
            'alignment text:label-followed-by="listtab" fo:text-indent='
            '"-0.635cm" fo:margin-left="1.905cm"/></style:list-level-'
            'properties></text:list-level-style-bullet></text:list-style>'
            '<text:list-style style:name="L2"><text:list-level-style-number '
            'text:level="1" style:num-suffix="." style:num-format="1">'
            '<style:list-level-properties text:list-level-position-and-space-'
            'mode="label-alignment"><style:list-level-label-alignment '
            'text:label-followed-by="listtab" fo:text-indent="-0.635cm" '
            'fo:margin-left="1.27cm"/></style:list-level-properties>'
            '</text:list-level-style-number><text:list-level-style-number '
            'text:level="2" style:num-suffix=")" style:num-format="a">'
            '<style:list-level-properties text:list-level-position-and-space-'
            'mode="label-alignment"><style:list-level-label-alignment '
            'text:label-followed-by="listtab" fo:text-indent="-0.635cm" '
            'fo:margin-left="1.905cm"/></style:list-level-properties>'
            '</text:list-level-style-number></text:list-style>'
            '</office:automatic-styles>')
    def item(text, sub=None):
        inner = '<text:p text:style-name="Text_20_body">%s</text:p>' % text
        return "<text:list-item>%s%s</text:list-item>" % (inner, sub or "")
    def cell(text, style="Table1.A2", cols=1, rows=1):
        attrs = ""
        if cols > 1:
            attrs += ' table:number-columns-spanned="%d"' % cols
        if rows > 1:
            attrs += ' table:number-rows-spanned="%d"' % rows
        return ('<table:table-cell table:style-name="%s"%s><text:p>%s'
                '</text:p></table:table-cell>' % (style, attrs, text))
    cov = "<table:covered-table-cell/>"
    body = "".join([
        '<text:h text:style-name="P0" text:outline-level="1">'
        'OpenDocument features</text:h>',
        '<text:p text:style-name="P1">Plain, <text:span text:style-name='
        '"T1">bold</text:span> and <text:span text:style-name="T2">red '
        'italic</text:span>   collapsed<text:s text:c="3"/>kept, a '
        '<text:a xlink:type="simple" xlink:href="https://example.com/">link'
        '</text:a> and a note<text:note text:id="ftn1" text:note-class='
        '"footnote"><text:note-citation>1</text:note-citation><text:note-'
        'body><text:p text:style-name="Footnote">The note at the foot.'
        '</text:p></text:note-body></text:note>.<text:tab/>After a tab'
        '<text:line-break/>After a break.</text:p>',
        '<text:p text:style-name="Text_20_body"><draw:frame draw:style-name='
        '"fr1" draw:name="Dot" text:anchor-type="paragraph" svg:width='
        '"2cm" svg:height="2cm"><draw:image xlink:href="Pictures/dot.png" '
        'xlink:type="simple"/></draw:frame>A picture floats to the right of '
        'this paragraph, which runs on beside it for a while longer so the '
        'wrap shows.</text:p>',
        '<text:list text:style-name="L1">%s%s</text:list>'
        % (item("First bullet"), item("Second bullet",
           '<text:list>%s</text:list>' % item("Nested bullet"))),
        '<text:list text:style-name="L2">%s%s%s</text:list>'
        % (item("One"), item("Two", '<text:list>%s%s</text:list>'
                             % (item("Two a"), item("Two b"))),
           item("Three")),
        '<text:h text:style-name="Heading_20_2" text:outline-level="2">'
        'A table</text:h>',
        '<table:table table:name="Table1" table:style-name="Table1">'
        '<table:table-column table:style-name="Table1.A" table:number-'
        'columns-repeated="3"/><table:table-header-rows><table:table-row>'
        '%s%s%s</table:table-row></table:table-header-rows><table:table-row>'
        '%s%s%s</table:table-row><table:table-row>%s%s%s</table:table-row>'
        '</table:table>' % (cell("Planet", "Table1.A1"),
                            cell("Moons", "Table1.A1"),
                            cell("Rings", "Table1.A1"),
                            cell("Saturn spans two", cols=2), cov,
                            cell("Merged", rows=2),
                            cell("Jupiter"), cell("95"), cov),
        '<text:p text:style-name="P3">Centred, with a bookmark<text:bookmark'
        ' text:name="here"/> in it.</text:p>',
        '<office:annotation><text:p>A comment, not shown</text:p>'
        '</office:annotation>',
        '<text:p text:style-name="P2">A landscape page</text:p>'])
    content = (XML + '<office:document-content %s>%s<office:body>'
               '<office:text>%s</office:text></office:body>'
               '</office:document-content>' % (O, auto, body))
    meta = (XML + '<office:document-meta %s><office:meta><dc:title>ODT '
            'features</dc:title><meta:initial-creator>gen_office'
            '</meta:initial-creator></office:meta></office:document-meta>'
            % O)
    manifest = (XML + '<manifest:manifest xmlns:manifest="urn:oasis:names:'
                'tc:opendocument:xmlns:manifest:1.0"><manifest:file-entry '
                'manifest:full-path="/" manifest:media-type="application/'
                'vnd.oasis.opendocument.text"/></manifest:manifest>')
    parts = {"mimetype": "application/vnd.oasis.opendocument.text",
             "content.xml": content, "styles.xml": O_STYLES,
             "meta.xml": meta, "META-INF/manifest.xml": manifest,
             "Pictures/dot.png": png(8, 8, (200, 80, 40))}
    return package(parts, "mimetype")


DOCS.append(Doc("odt_features", ".odt", "OpenDocument: styles, a note, a "
                "frame, lists, outline numbers, a table, masters",
                odt_features(), 2,
                ["1 OpenDocument features", "bold", "red italic",
                 "collapsed   kept", "link", "After a tab",
                 "After a break", "First bullet", "Nested bullet", "1.",
                 "a)", "Two b", "1.1 A table", "Saturn spans two", "Merged",
                 "Header of the text", "Page 1 of 2",
                 "The note at the foot"]))

# ------------------------------------------------------------- PowerPoint

def sp(idx, ph, name, text_xml="", xfrm=None, extra=""):
    """A slide shape: a placeholder of type @p ph (idx @p idx; "" for one
    of no type), or with @p ph None not one."""
    phx = ""
    if ph is not None:
        phx = '<p:ph%s%s/>' % (' type="%s"' % ph if ph else "",
                               ' idx="%d"' % idx if idx else "")
    sppr = ""
    if xfrm:
        sppr = ('<a:xfrm><a:off x="%d" y="%d"/><a:ext cx="%d" cy="%d"/>'
                '</a:xfrm>' % xfrm) + extra
    body = ('<p:txBody><a:bodyPr/><a:lstStyle/>%s</p:txBody>' % text_xml
            if text_xml is not None else "")
    return ('<p:sp><p:nvSpPr><p:cNvPr id="%d" name="%s"/><p:cNvSpPr/>'
            '<p:nvPr>%s</p:nvPr></p:nvSpPr><p:spPr>%s</p:spPr>%s</p:sp>'
            % (idx + 10, name, phx, sppr, body))


def para(text, lvl=0):
    return ('<a:p><a:pPr lvl="%d"/><a:r><a:rPr lang="en-GB"/><a:t>%s</a:t>'
            '</a:r></a:p>' % (lvl, text))


def tree(shapes):
    return ('<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/>'
            '<p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>%s'
            '</p:spTree></p:cSld>' % "".join(shapes))


MASTER_TX = ('<p:txStyles><p:titleStyle><a:lvl1pPr algn="l"><a:defRPr '
             'sz="4400"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill>'
             '<a:latin typeface="+mj-lt"/></a:defRPr></a:lvl1pPr>'
             '</p:titleStyle><p:bodyStyle><a:lvl1pPr marL="228600" '
             'indent="-228600"><a:buFont typeface="Arial"/><a:buChar '
             'char="•"/><a:defRPr sz="2800"><a:solidFill><a:schemeClr '
             'val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/>'
             '</a:defRPr></a:lvl1pPr><a:lvl2pPr marL="685800" '
             'indent="-228600"><a:buFont typeface="Arial"/><a:buChar '
             'char="–"/><a:defRPr sz="2400"><a:solidFill><a:schemeClr '
             'val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/>'
             '</a:defRPr></a:lvl2pPr></p:bodyStyle><p:otherStyle><a:lvl1pPr>'
             '<a:defRPr sz="1800"/></a:lvl1pPr></p:otherStyle></p:txStyles>')

EMU_W, EMU_H = 12192000, 6858000
def pptx_features():
    master = (XML + '<p:sldMaster %s><p:cSld><p:bg><p:bgRef idx="1001">'
              '<a:schemeClr val="bg1"/></p:bgRef></p:bg><p:spTree>'
              '<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/>'
              '</p:nvGrpSpPr><p:grpSpPr/>%s%s%s</p:spTree></p:cSld>'
              '<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" '
              'accent1="accent1" accent2="accent2" accent3="accent3" '
              'accent4="accent4" accent5="accent5" accent6="accent6" '
              'hlink="hlink" folHlink="folHlink"/><p:sldLayoutIdLst>'
              '<p:sldLayoutId id="2147483649" r:id="rId1"/><p:sldLayoutId '
              'id="2147483650" r:id="rId2"/></p:sldLayoutIdLst>%s'
              '</p:sldMaster>'
              % (P, sp(0, "title", "Title", "", (838200, 365125, 10515600,
                                                  1325563)),
                 sp(1, "body", "Body", "", (838200, 1825625, 10515600,
                                            4351338)),
                 sp(12, "sldNum", "Number", '<a:p><a:pPr algn="r"/><a:fld '
                    'id="{0}" type="slidenum"><a:rPr/><a:t>&#8249;#&#8250;'
                    '</a:t></a:fld></a:p>', (8610600, 6356350, 2743200,
                                              365125)),
                 MASTER_TX))
    title_layout = (XML + '<p:sldLayout %s type="title">%s'
                    '<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>'
                    '</p:sldLayout>'
                    % (P, tree([sp(0, "ctrTitle", "Title", "",
                                   (1524000, 1122363, 9144000, 2387600)),
                                sp(1, "subTitle", "Subtitle", "",
                                   (1524000, 3602038, 9144000, 1655762))])))
    content_layout = (XML + '<p:sldLayout %s type="obj">%s</p:sldLayout>'
                      % (P, tree([sp(0, "title", "Title", ""),
                                  sp(1, "", "Content", "")])))
    slide1 = (XML + '<p:sld %s>%s</p:sld>'
              % (P, tree([sp(0, "ctrTitle", "Title", para("Slides features")),
                          sp(1, "subTitle", "Subtitle",
                             para("A deck made by hand"))])))
    table = ('<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id="30" '
             'name="Table"/><p:cNvGraphicFramePr/><p:nvPr/>'
             '</p:nvGraphicFramePr><p:xfrm><a:off x="6400800" y="1825625"/>'
             '<a:ext cx="4953000" cy="1112520"/></p:xfrm><a:graphic>'
             '<a:graphicData uri="http://schemas.openxmlformats.org/'
             'drawingml/2006/table"><a:tbl><a:tblPr firstRow="1" '
             'bandRow="1"/><a:tblGrid><a:gridCol w="2476500"/><a:gridCol '
             'w="2476500"/></a:tblGrid>%s</a:tbl></a:graphicData></a:graphic>'
             '</p:graphicFrame>'
             % "".join('<a:tr h="370840">%s</a:tr>' % "".join(
                 '<a:tc><a:txBody><a:bodyPr/><a:lstStyle/>%s</a:txBody>'
                 '<a:tcPr><a:lnB w="12700"><a:solidFill><a:srgbClr '
                 'val="000000"/></a:solidFill></a:lnB>%s</a:tcPr></a:tc>'
                 % (para(c), '<a:solidFill><a:schemeClr val="accent1"/>'
                    '</a:solidFill>' if r == 0 else "")
                 for c in row) for r, row in enumerate(
                     [["Planet", "Moons"], ["Mars", "2"]])))
    picture = ('<p:pic><p:nvPicPr><p:cNvPr id="31" name="Dot"/><p:cNvPicPr/>'
               '<p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed="rId2"/>'
               '<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>'
               '<a:xfrm><a:off x="6400800" y="3429000"/><a:ext cx="914400" '
               'cy="914400"/></a:xfrm><a:prstGeom prst="ellipse"><a:avLst/>'
               '</a:prstGeom></p:spPr></p:pic>')
    group = ('<p:grpSp><p:nvGrpSpPr><p:cNvPr id="32" name="Group"/>'
             '<p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm '
             'rot="600000"><a:off x="7620000" y="4572000"/><a:ext '
             'cx="2743200" cy="914400"/><a:chOff x="0" y="0"/><a:chExt '
             'cx="2743200" cy="914400"/></a:xfrm></p:grpSpPr>%s</p:grpSp>'
             % sp(20, None, "Arrow", para("Grouped arrow"),
                  (0, 0, 2743200, 914400),
                  '<a:prstGeom prst="rightArrow"><a:avLst/></a:prstGeom>'
                  '<a:solidFill><a:schemeClr val="accent2"/></a:solidFill>'))
    slide2 = (XML + '<p:sld %s>%s</p:sld>'
              % (P, tree([sp(0, "title", "Title", para("Second slide")),
                          sp(1, "", "Content", para("A first point") +
                             para("A point under it", 1) +
                             para("Back again"),
                             (838200, 1825625, 5181600, 4351338)),
                          table, picture, group])))
    frame = ('<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id="40" '
             'name="Diagram"/><p:cNvGraphicFramePr/><p:nvPr/>'
             '</p:nvGraphicFramePr><p:xfrm><a:off x="1524000" y="2286000"/>'
             '<a:ext cx="9144000" cy="1828800"/></p:xfrm><a:graphic>'
             '<a:graphicData uri="http://schemas.openxmlformats.org/'
             'drawingml/2006/diagram"><dgm:relIds xmlns:dgm="http://'
             'schemas.openxmlformats.org/drawingml/2006/diagram" r:dm="rId2" '
             'r:lo="rId3" r:qs="rId4" r:cs="rId5"/></a:graphicData>'
             '</a:graphic></p:graphicFrame>')
    slide3 = (XML + '<p:sld %s>%s</p:sld>'
              % (P, tree([sp(0, "title", "Title", para("A diagram")),
                          frame])))
    pres = (XML + '<p:presentation %s><p:sldMasterIdLst><p:sldMasterId '
            'id="2147483648" r:id="rId1"/></p:sldMasterIdLst><p:sldIdLst>'
            '<p:sldId id="256" r:id="rId2"/><p:sldId id="257" r:id="rId3"/>'
            '<p:sldId id="258" r:id="rId5"/></p:sldIdLst><p:sldSz cx="%d" '
            'cy="%d"/><p:notesSz cx="6858000" cy="9144000"/>'
            '</p:presentation>' % (P, EMU_W, EMU_H))
    parts = {
        "[Content_Types].xml": types([
            ("ppt/presentation.xml", PML + "presentation.main+xml"),
            ("ppt/slideMasters/slideMaster1.xml", PML + "slideMaster+xml"),
            ("ppt/slideLayouts/slideLayout1.xml", PML + "slideLayout+xml"),
            ("ppt/slideLayouts/slideLayout2.xml", PML + "slideLayout+xml"),
            ("ppt/slides/slide1.xml", PML + "slide+xml"),
            ("ppt/slides/slide2.xml", PML + "slide+xml"),
            ("ppt/slides/slide3.xml", PML + "slide+xml"),
            ("ppt/diagrams/data1.xml", "application/vnd.openxmlformats-"
             "officedocument.drawingml.diagramData+xml"),
            ("ppt/diagrams/drawing1.xml",
             "application/vnd.ms-office.drawingml.diagramDrawing+xml"),
            ("ppt/theme/theme1.xml",
             "application/vnd.openxmlformats-officedocument.theme+xml")]),
        "_rels/.rels": rels([("rId1", "officeDocument",
                              "ppt/presentation.xml")]),
        "ppt/_rels/presentation.xml.rels": rels([
            ("rId1", "slideMaster", "slideMasters/slideMaster1.xml"),
            ("rId2", "slide", "slides/slide1.xml"),
            ("rId3", "slide", "slides/slide2.xml"),
            ("rId4", "theme", "theme/theme1.xml"),
            ("rId5", "slide", "slides/slide3.xml")]),
        "ppt/presentation.xml": pres,
        "ppt/slideMasters/slideMaster1.xml": master,
        "ppt/slideMasters/_rels/slideMaster1.xml.rels": rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "slideLayout", "../slideLayouts/slideLayout2.xml"),
            ("rId3", "theme", "../theme/theme1.xml")]),
        "ppt/slideLayouts/slideLayout1.xml": title_layout,
        "ppt/slideLayouts/slideLayout2.xml": content_layout,
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels": rels([
            ("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")]),
        "ppt/slideLayouts/_rels/slideLayout2.xml.rels": rels([
            ("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")]),
        "ppt/slides/slide1.xml": slide1, "ppt/slides/slide2.xml": slide2,
        "ppt/slides/slide3.xml": slide3,
        "ppt/slides/_rels/slide3.xml.rels": rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout2.xml"),
            ("rId2", "diagramData", "../diagrams/data1.xml"),
            ("rId6", "http://schemas.microsoft.com/office/2007/"
             "relationships/diagramDrawing", "../diagrams/drawing1.xml")]),
        "ppt/diagrams/data1.xml": DIAGRAM_DATA,
        "ppt/diagrams/drawing1.xml": diagram_drawing(),
        "ppt/slides/_rels/slide1.xml.rels": rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml")]),
        "ppt/slides/_rels/slide2.xml.rels": rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout2.xml"),
            ("rId2", "image", "../media/dot.png")]),
        "ppt/theme/theme1.xml": THEME,
        "ppt/media/dot.png": png(8, 8, (60, 160, 90))}
    return package(parts)


DOCS.append(Doc("pptx_features", ".pptx", "PowerPoint: a master, two "
                "layouts, placeholders, bullets, a table, a picture, a "
                "turned group, a diagram's saved drawing", pptx_features(), 3,
                ["Slides features", "A deck made by hand"]))

# ----------------------------------------------------------------- hostile

def docx_with(body_xml, styles=W_STYLES):
    """A Word package of one body, and styles; nothing else."""
    return package({
        "[Content_Types].xml": types([("word/document.xml",
                                       WML + "document.main+xml"),
                                      ("word/styles.xml",
                                       WML + "styles+xml")]),
        "_rels/.rels": rels([("rId1", "officeDocument",
                              "word/document.xml")]),
        "word/_rels/document.xml.rels": rels([("rId1", "styles",
                                               "styles.xml")]),
        "word/document.xml": body_xml, "word/styles.xml": styles})


def odt_with(content, styles=O_STYLES):
    return package({"mimetype": "application/vnd.oasis.opendocument.text",
                    "content.xml": content, "styles.xml": styles},
                   "mimetype")


DOCS.append(Doc("not_office", ".docx", "A ZIP holding no document",
                package({"readme.txt": "nothing here"}), 0, []))
DOCS.append(Doc("ole_locked", ".docx", "An OLE2 compound file: Word "
                "97-2003, or a document locked with a password",
                bytes([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1]) +
                bytes(504), 0, []))
DOCS.append(Doc("truncated", ".docx", "A Word file cut short",
                docx_features()[:900], 0, []))
DOCS.append(Doc("bad_xml", ".docx", "A Word body that is not XML",
                docx_with(XML + "<w:document %s><w:body><w:p><w:r>" % W),
                0, []))

nest = "".join('<w:tbl><w:tblGrid><w:gridCol w:w="2000"/></w:tblGrid><w:tr>'
               '<w:tc>' for _ in range(40))
unnest = "".join("<w:p/></w:tc></w:tr></w:tbl>" for _ in range(40))
DOCS.append(Doc("deep_tables", ".docx", "Tables forty deep: laid out to "
                "the nesting kept, the rest let go",
                docx_with(XML + "<w:document %s><w:body>%s%s%s</w:body>"
                          "</w:document>" % (W, nest, wp("Deep inside"),
                                             unnest)), -1, []))

huge = ('<w:p><w:pPr><w:ind w:left="-999999999" w:hanging="999999999"/>'
        '<w:spacing w:before="999999999" w:line="-5" w:lineRule="exact"/>'
        '<w:tabs><w:tab w:val="right" w:pos="999999999"/></w:tabs></w:pPr>'
        '<w:r><w:rPr><w:sz w:val="999999999"/><w:spacing w:val='
        '"-999999"/><w:w w:val="0"/></w:rPr><w:t>Huge\tvalues</w:t></w:r>'
        '</w:p><w:sectPr><w:pgSz w:w="0" w:h="0"/><w:pgMar w:top="-99999" '
        'w:left="99999" w:right="99999" w:bottom="99999"/><w:cols '
        'w:num="10000" w:space="-5"/></w:sectPr>')
DOCS.append(Doc("huge_values", ".docx", "Sizes, indents, pages and "
                "columns out of all reason",
                docx_with(XML + "<w:document %s><w:body>%s</w:body>"
                          "</w:document>" % (W, huge)), -1, []))

cyc = O_STYLES.replace('<style:style style:name="Standard" '
                       'style:family="paragraph"/>',
                       '<style:style style:name="Standard" style:family='
                       '"paragraph" style:parent-style-name="Loop"/>'
                       '<style:style style:name="Loop" style:family='
                       '"paragraph" style:parent-style-name="Standard">'
                       '<style:text-properties fo:font-size="200%"/>'
                       '</style:style>')
rows = ('<table:table><table:table-column table:number-columns-repeated='
        '"1000000"/><table:table-row table:number-rows-repeated="1000000">'
        '<table:table-cell table:number-columns-repeated="1000000" '
        'table:number-columns-spanned="999999" table:number-rows-spanned='
        '"999999"><text:p>Cell</text:p></table:table-cell></table:table-row>'
        '</table:table><text:list text:style-name="L1"><text:list-item>'
        '<text:list><text:list-item><text:list><text:list-item><text:list>'
        '<text:list-item><text:list><text:list-item><text:list>'
        '<text:list-item><text:list><text:list-item><text:list>'
        '<text:list-item><text:list><text:list-item><text:list>'
        '<text:list-item><text:list><text:list-item><text:list>'
        '<text:list-item><text:p>Twelve deep</text:p></text:list-item>'
        + "</text:list></text:list-item>" * 11 + '</text:list>')
DOCS.append(Doc("odt_cycles", ".odt", "Styles based on each other, tables "
                "repeated a million times, lists twelve deep",
                odt_with(XML + '<office:document-content %s><office:body>'
                         '<office:text><text:p text:style-name="Standard">'
                         'Loops</text:p>%s</office:text></office:body>'
                         '</office:document-content>' % (O, rows), cyc),
                -1, []))

laughs = ('<?xml version="1.0"?><!DOCTYPE w:document [<!ENTITY a "aaaaaaaa">'
          '<!ENTITY b "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;"><!ENTITY c "&b;&b;'
          '&b;&b;&b;&b;&b;&b;&b;&b;">]><w:document %s><w:body><w:p><w:r>'
          '<w:t>&c;</w:t></w:r></w:p></w:body></w:document>' % W)
DOCS.append(Doc("entities", ".docx", "Entities declared to multiply: "
                "never expanded", docx_with(laughs), -1, []))

# ----------------------------------------------------------------- charts

C_NS = ('xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart" '
        'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/'
        'relationships"')
C_TYPE = "application/vnd.openxmlformats-officedocument.drawingml.chart+xml"
C_AXES = ('<c:catAx><c:axId val="1"/><c:scaling><c:orientation val="minMax"/>'
          '</c:scaling><c:delete val="0"/><c:axPos val="b"/><c:crossAx '
          'val="2"/></c:catAx><c:valAx><c:axId val="2"/><c:scaling>'
          '<c:orientation val="minMax"/></c:scaling><c:delete val="0"/>'
          '<c:axPos val="l"/><c:majorGridlines/><c:crossAx val="1"/>'
          '<c:crossBetween val="between"/></c:valAx>')


def c_cache(vals):
    """Values cached as a chart keeps them: words, or numbers."""
    pts = "".join('<c:pt idx="%d"><c:v>%s</c:v></c:pt>' % (i, v)
                  for i, v in enumerate(vals))
    if isinstance(vals[0], str):
        return ('<c:strRef><c:f>Sheet1!A1</c:f><c:strCache><c:ptCount '
                'val="%d"/>%s</c:strCache></c:strRef>' % (len(vals), pts))
    return ('<c:numRef><c:f>Sheet1!B1</c:f><c:numCache><c:formatCode>'
            'General</c:formatCode><c:ptCount val="%d"/>%s</c:numCache>'
            '</c:numRef>' % (len(vals), pts))


def c_ser(i, name, cats, vals, extra=""):
    """A series: its name, its categories and its values."""
    return ('<c:ser><c:idx val="%d"/><c:order val="%d"/><c:tx>%s</c:tx>%s'
            '<c:cat>%s</c:cat><c:val>%s</c:val></c:ser>'
            % (i, i, c_cache([name]), extra, c_cache(cats), c_cache(vals)))


def c_part(title, plot, legend="b"):
    """A chart part: a title over a plot area, a legend, 12 pt text."""
    return (XML + '<c:chartSpace %s><c:chart><c:title><c:tx><c:rich>'
            '<a:bodyPr/><a:lstStyle/><a:p><a:r><a:t>%s</a:t></a:r></a:p>'
            '</c:rich></c:tx><c:overlay val="0"/></c:title>'
            '<c:autoTitleDeleted val="0"/><c:plotArea><c:layout/>%s'
            '</c:plotArea><c:legend><c:legendPos val="%s"/></c:legend>'
            '</c:chart><c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr>'
            '<a:defRPr sz="1200"/></a:pPr></a:p></c:txPr></c:chartSpace>'
            % (C_NS, title, plot, legend))


C_SHARES = ('<c:dLbls><c:showVal val="0"/><c:showCatName val="0"/>'
            '<c:showSerName val="0"/><c:showPercent val="1"/></c:dLbls>')
C_COLUMN = c_part("Quarterly sales", '<c:barChart><c:barDir val="col"/>'
                  '<c:grouping val="clustered"/>%s%s<c:axId val="1"/>'
                  '<c:axId val="2"/></c:barChart>%s'
                  % (c_ser(0, "North", ["Q1", "Q2", "Q3", "Q4"],
                           [20, 35, 30, 25]),
                     c_ser(1, "South", ["Q1", "Q2", "Q3", "Q4"],
                           [15, 22, 28, 32]), C_AXES))
C_PIE = c_part("Where the time goes", '<c:pieChart><c:varyColors val="1"/>'
               '%s</c:pieChart>' % c_ser(0, "Hours", ["Reading", "Writing",
                                                      "Meetings"],
                                         [4, 2, 2], C_SHARES), "r")
C_SURFACE = c_part("Not drawn here", '<c:surfaceChart>%s<c:axId val="1"/>'
                   '<c:axId val="2"/></c:surfaceChart>%s'
                   % (c_ser(0, "Z", ["a", "b"], [1, 2]), C_AXES))


def c_frame(n, rid):
    """A slide's frame holding chart @p rid, below its title."""
    return ('<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id="%d" '
            'name="Chart %d"/><p:cNvGraphicFramePr/><p:nvPr/>'
            '</p:nvGraphicFramePr><p:xfrm><a:off x="1524000" y="1600000"/>'
            '<a:ext cx="9144000" cy="4800000"/></p:xfrm><a:graphic>'
            '<a:graphicData uri="http://schemas.openxmlformats.org/'
            'drawingml/2006/chart"><c:chart %s r:id="%s"/></a:graphicData>'
            '</a:graphic></p:graphicFrame>' % (50 + n, n, C_NS, rid))


def pptx_charts():
    """Three slides of charts: columns, a pie, a kind not drawn here."""
    parts = {
        "_rels/.rels": rels([("rId1", "officeDocument",
                              "ppt/presentation.xml")]),
        "ppt/slideMasters/slideMaster1.xml": XML + (
            '<p:sldMaster %s>%s<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" '
            'tx2="dk2" accent1="accent1" accent2="accent2" '
            'accent3="accent3" accent4="accent4" accent5="accent5" '
            'accent6="accent6" hlink="hlink" folHlink="folHlink"/>'
            '<p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/>'
            '</p:sldLayoutIdLst>%s</p:sldMaster>'
            % (P, tree([sp(0, "title", "Title", "",
                           (838200, 365125, 10515600, 1325563))]),
               MASTER_TX)),
        "ppt/slideMasters/_rels/slideMaster1.xml.rels": rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "theme", "../theme/theme1.xml")]),
        "ppt/slideLayouts/slideLayout1.xml": XML + (
            '<p:sldLayout %s type="titleOnly">%s</p:sldLayout>'
            % (P, tree([sp(0, "title", "Title", "")]))),
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels": rels([
            ("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")]),
        "ppt/theme/theme1.xml": THEME}
    over = [("ppt/presentation.xml", PML + "presentation.main+xml"),
            ("ppt/slideMasters/slideMaster1.xml", PML + "slideMaster+xml"),
            ("ppt/slideLayouts/slideLayout1.xml", PML + "slideLayout+xml"),
            ("ppt/theme/theme1.xml",
             "application/vnd.openxmlformats-officedocument.theme+xml")]
    ids, prels = [], [("rId1", "slideMaster",
                       "slideMasters/slideMaster1.xml")]
    for n, (title, part) in enumerate([("Sales", C_COLUMN), ("Time", C_PIE),
                                       ("", C_SURFACE)], 1):
        parts["ppt/slides/slide%d.xml" % n] = XML + (
            '<p:sld %s>%s</p:sld>'
            % (P, tree([sp(0, "title", "Title", para(title) if title
                           else ""), c_frame(n, "rId2")])))
        parts["ppt/slides/_rels/slide%d.xml.rels" % n] = rels([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "chart", "../charts/chart%d.xml" % n)])
        parts["ppt/charts/chart%d.xml" % n] = part
        over += [("ppt/slides/slide%d.xml" % n, PML + "slide+xml"),
                 ("ppt/charts/chart%d.xml" % n, C_TYPE)]
        ids.append('<p:sldId id="%d" r:id="rId%d"/>' % (255 + n, n + 1))
        prels.append(("rId%d" % (n + 1), "slide", "slides/slide%d.xml" % n))
    parts["ppt/presentation.xml"] = XML + (
        '<p:presentation %s><p:sldMasterIdLst><p:sldMasterId '
        'id="2147483648" r:id="rId1"/></p:sldMasterIdLst><p:sldIdLst>%s'
        '</p:sldIdLst><p:sldSz cx="%d" cy="%d"/></p:presentation>'
        % (P, "".join(ids), EMU_W, EMU_H))
    parts["ppt/_rels/presentation.xml.rels"] = rels(prels)
    parts["[Content_Types].xml"] = types(over)
    return package(parts)


def docx_chart():
    """A Word page with a pie chart in its text."""
    chart = ('<w:r><w:drawing><wp:inline><wp:extent cx="5486400" '
             'cy="3200400"/><wp:docPr id="1" name="Chart 1"/><a:graphic>'
             '<a:graphicData uri="http://schemas.openxmlformats.org/'
             'drawingml/2006/chart"><c:chart %s r:id="rIdChart"/>'
             '</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>'
             % C_NS)
    doc = (XML + '<w:document %s><w:body>%s%s<w:sectPr><w:pgSz w:w="12240" '
           'w:h="15840"/><w:pgMar w:top="1440" w:right="1440" '
           'w:bottom="1440" w:left="1440"/></w:sectPr></w:body>'
           '</w:document>' % (W, wp("A chart in the text"),
                              wp(None, runs=chart)))
    return package({
        "[Content_Types].xml": types([
            ("word/document.xml", WML + "document.main+xml"),
            ("word/styles.xml", WML + "styles+xml"),
            ("word/theme/theme1.xml",
             "application/vnd.openxmlformats-officedocument.theme+xml"),
            ("word/charts/chart1.xml", C_TYPE)]),
        "_rels/.rels": rels([("rId1", "officeDocument",
                              "word/document.xml")]),
        "word/_rels/document.xml.rels": rels([
            ("rIdStyles", "styles", "styles.xml"),
            ("rIdTheme", "theme", "theme/theme1.xml"),
            ("rIdChart", "chart", "charts/chart1.xml")]),
        "word/document.xml": doc, "word/styles.xml": W_STYLES,
        "word/theme/theme1.xml": THEME,
        "word/charts/chart1.xml": C_PIE.replace('sz="1200"', 'sz="1000"')})


DOCS.append(Doc("pptx_charts", ".pptx", "PowerPoint charts drawn from "
                "their cached values: columns, a pie, a surface left grey",
                pptx_charts(), 3,
                ["Sales", "Quarterly sales", "Q1", "Q4", "North", "South",
                 "40"]))
DOCS.append(Doc("docx_chart", ".docx", "A Word page with a pie chart in its "
                "text", docx_chart(), 1,
                ["A chart in the text", "Where the time goes", "Reading",
                 "Meetings", "50%", "25%"]))

# -------------------------------------------------------------------- out

def c_string(text):
    """@p text as a C string literal's inside: UTF-8, octal past ASCII."""
    out = []
    for b in text.encode("utf-8"):
        c = chr(b)
        if c == "\n":
            out.append("\\n")
        elif c in "\"\\":
            out.append("\\" + c)
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append("\\%03o" % b)
    return "".join(out)


out = ["/* test_office's documents, by tests/fixtures/tools/gen_office.py:",
       " * base64, each with the pages it lays out to (0: refused, -1: any)",
       " * and words its first page shows, a line each.",
       " * SPDX-License-Identifier: Apache-2.0 */"]
for d in DOCS:
    b64 = base64.b64encode(d.data).decode()
    out.append("/* %s */" % d.doc)
    out.append("static const char fx_%s[] = /* %d bytes */"
               % (d.name, len(d.data)))
    out += ['    "%s"' % b64[i:i + 72] for i in range(0, len(b64), 72)]
    out[-1] += ";"
out.append("static const struct {")
out.append("    const char *name, *ext, *data;")
out.append("    int pages;")
out.append("    const char *words;")
out.append("} fixtures[] = {")
for d in DOCS:
    out.append('    { "%s", "%s", fx_%s, %d,' % (d.name, d.ext, d.name,
                                                  d.pages))
    out.append('      "%s" },' % c_string("".join(w + "\n"
                                                  for w in d.words)))
out.append("};")
open(OUT, "w").write("\n".join(out) + "\n")
if DIR:
    os.makedirs(DIR, exist_ok=True)
    for d in DOCS:
        open(os.path.join(DIR, d.name + d.ext), "wb").write(d.data)
print("%d documents, %d bytes" % (len(DOCS), sum(len(d.data)
                                                 for d in DOCS)))
