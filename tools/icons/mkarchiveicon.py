"""Emit TikuArchive's HVIF artwork as C arrays: the application's parcel,
a cardboard box with a zipper down its front, and the document icon
Tracker gives archives, a page with the same zipper.

Drawn as the rest of the set: a dark edge, faces shaded from the lit top
left, a highlight.  Run from tools/icons, into
kits/translation/tiku_icons_archive.inl.
SPDX-License-Identifier: Apache-2.0
"""
from mkhvif import rect, header
from mktoolbaricons import Art, BOX, page

EDGE, FACE, LIT, SIDE = BOX
TAPE = (240, 222, 180, 255)
METAL = [(0.0, (236, 238, 242, 255)), (1.0, (150, 156, 166, 255))]
TEETH_DARK = (58, 60, 68, 255)
TEETH_LIT = (196, 198, 206, 255)


def zipper(art, cx, y0, y1, w):
    """A zipper down @p cx from @p y0 to @p y1: a dark band, teeth
    either side in turn, and its pull hanging at the top."""
    art.solid(TEETH_DARK, rect(cx - w, y0, cx + w, y1))
    y, left = y0 + 2, True
    while y + 2 <= y1 - 1:
        x = cx - w + 0.5 if left else cx + 0.5
        art.solid(TEETH_LIT, rect(x, y, x + w - 1, y + 2))
        y, left = y + 3, not left
    art.solid(TEETH_DARK,
              [(cx - 4, y0 + 1), (cx + 4, y0 + 1), (cx + 3, y0 + 12),
               (cx - 3, y0 + 12)])
    art.shaded((cx - 3, y0 + 2), (cx + 3, y0 + 11), METAL,
               [(cx - 3, y0 + 2), (cx + 3, y0 + 2), (cx + 2, y0 + 11),
                (cx - 2, y0 + 11)])
    art.solid(TEETH_DARK, rect(cx - 1, y0 + 7, cx + 1, y0 + 9))


def parcel(art):
    """The box: its top and side faces behind its front, tape over the
    top, and the zipper down the front."""
    art.solid(EDGE, [(6, 26), (18, 14), (58, 14), (58, 48), (46, 60),
                     (6, 60)])
    art.solid(LIT, [(9, 25), (19, 16), (55, 16), (45, 25)])
    art.solid(SIDE, [(47, 27), (56, 18), (56, 47), (47, 57)])
    art.shaded((8, 27), (45, 59), FACE, rect(8, 27, 45, 58))
    art.solid(TAPE, [(22, 25), (30, 25), (40, 16), (32, 16)])
    art.solid((255, 246, 222, 255), rect(8, 27, 45, 29))
    zipper(art, 26.5, 27, 58, 3)


def archive_file(art):
    """A page with a zipper down its middle: an archive, in Tracker."""
    page(art, 10, 4, 54, 60, [(52, 16, 26), (52, 38, 48)])
    zipper(art, 32, 6, 58, 3)


ICONS = []
for name, draw in (("archive_viewer", parcel), ("archive_file", archive_file)):
    a = Art()
    draw(a)
    ICONS.append((name, a.blob()))

print(header("tiku_icons_archive.inl",
             "HVIF bytes for TikuArchive's icon, a cardboard box with a "
             "zipper (archive_viewer), and an archive file's icon, a page "
             "with the same zipper (archive_file); tiku_icons_tiku.c "
             "includes them.",
             "tools/icons/mkarchiveicon.py"))
for name, blob in ICONS:
    print(f"static const uint8_t tiku_icon_{name}[] = {{")
    for at in range(0, len(blob), 12):
        print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at + 12]) + ",")
    print("};\n")
