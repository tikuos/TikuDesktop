#!/usr/bin/env python3
# TikuDesktop Translation Kit tests.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# test_richhtml.py - independent HTML parser checks for document export.
#
# SPDX-License-Identifier: Apache-2.0

"""Validate emitted structure and decoded values with Python's HTML parser."""

from html.parser import HTMLParser
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


class Document(HTMLParser):
    """Check explicit nesting and retain semantic output for assertions."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.text = []
        self.links = []
        self.values = []
        self.tables = 0
        self.nested = 0
        self.headers = 0
        self.fonts = []
        self.images = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        assert tag in {"html", "head", "meta", "title", "style", "body",
                       "p", "span", "br", "a", "ol", "ul", "li", "table",
                       "colgroup", "col", "thead", "tbody", "tr", "td", "th",
                       "img"}
        assert not any(k.startswith("on") for k in attrs)
        if tag in {"ol", "ul"}:
            assert self.stack[-1] in {"body", "li", "td", "th"}
        if tag == "li":
            assert self.stack[-1] in {"ol", "ul"}
            if "value" in attrs:
                self.values.append(int(attrs["value"]))
        if tag == "table":
            assert self.stack[-1] in {"body", "td", "th"}
            self.tables += 1
            self.nested += self.stack[-1] in {"td", "th"}
        if tag in {"td", "th"}:
            assert self.stack[-1] == "tr"
            self.headers += tag == "th"
        if tag == "a":
            self.links.append(attrs["href"])
        if tag == "span":
            self.fonts.append(attrs["style"])
        if tag == "img":
            self.images.append(attrs)
        if tag not in {"meta", "br", "col", "img"}:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        assert self.stack and self.stack.pop() == tag, (tag, self.stack)

    def handle_data(self, data):
        if "body" in self.stack:
            self.text.append(data)


def picture(attrs, width=2, height=2, expected=None):
    """Decode the data URI, validate PNG CRCs and compare RGBA scanlines."""
    prefix = "data:image/png;base64,"
    assert attrs["src"].startswith(prefix)
    png = base64.b64decode(attrs["src"][len(prefix):], validate=True)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    at, chunks = 8, []
    while at < len(png):
        length = struct.unpack_from(">I", png, at)[0]
        kind = png[at + 4:at + 8]
        data = png[at + 8:at + 8 + length]
        crc = struct.unpack_from(">I", png, at + 8 + length)[0]
        assert zlib.crc32(kind + data) == crc
        chunks.append((kind, data))
        at += length + 12
    assert at == len(png)
    assert [kind for kind, _ in chunks] == [b"IHDR", b"IDAT", b"IEND"]
    assert chunks[0][1] == struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    assert not chunks[-1][1]
    if expected is None:
        expected = bytes.fromhex("00 0088aaff 12345680 00 11223300 ffffffff")
    assert zlib.decompress(chunks[1][1]) == expected


def gallery(path):
    """Check every pixel through independent base64 and zlib decoders."""
    parser = Document()
    parser.feed(path.read_text(encoding="utf-8"))
    parser.close()
    dimensions = [(1, 1), (2, 1), (3, 1), (7, 9), (1024, 1),
                  (1, 4096), (4095, 3)]
    assert len(parser.images) == len(dimensions) and not parser.stack
    assert parser.links == ["https://example.org/picture"] * len(dimensions)
    padding = set()
    for attrs, (width, height) in zip(parser.images, dimensions):
        assert attrs["alt"] == "Picture"
        assert attrs["style"] == "width:80.000pt;height:60.000pt"
        padding.add(len(attrs["src"]) - len(attrs["src"].rstrip("=")))
        expected = bytearray()
        for y in range(height):
            expected.append(0)
            for x in range(width):
                argb = ((y * width + x) * 0x739ab123) & 0xffffffff
                expected.extend(((argb >> 16) & 255, (argb >> 8) & 255,
                                 argb & 255, argb >> 24))
        picture(attrs, width, height, expected)
    assert padding == {0, 1, 2}


def main():
    """Generate the C fixture under an isolated directory and parse it."""
    with tempfile.TemporaryDirectory(prefix="tiku-html-parser-") as work:
        path = Path(work) / "fixture.html"
        images = Path(work) / "pictures.html"
        subprocess.run([sys.argv[1], str(path), str(images)], check=True)
        parser = Document()
        parser.feed(path.read_text(encoding="utf-8"))
        parser.close()
        assert not parser.stack
        assert parser.tables == 2 and parser.nested == 1 and parser.headers == 2
        assert parser.values == [4, 1, 5, 1, 12]
        assert parser.links == ['https://example.org/?a=1&b=%20"q']
        assert 'A<&%\"\'\té\nZ' in "".join(parser.text)
        assert "[Image:" not in "".join(parser.text)
        assert len(parser.images) == 1
        assert parser.images[0]["alt"] == '<photo & "alt">'
        assert parser.images[0]["style"] == "width:40.000pt;height:30.000pt"
        picture(parser.images[0])
        assert "font-weight:normal;" in parser.fonts[0]
        assert "\\22 \\3b \\7d \\3c " in parser.fonts[0]
        assert "font-size:13.125pt" in parser.fonts[0]
        gallery(images)
    print("HTML independent parser: structure, escaping and RGBA PNG PASS")


if __name__ == "__main__":
    main()
