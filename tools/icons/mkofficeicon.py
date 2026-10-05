"""Emit TikuDoc's and TikuSlides' HVIF artwork as C arrays: two sheets of
text under a blue heading for documents, an easel holding a chart for
slides, and the document icons Tracker gives their files.

Drawn as the rest of the set: a dark edge, faces shaded from the lit top
left, a highlight.  Run from tools/icons, into
kits/translation/tiku_icons_office.inl.
SPDX-License-Identifier: Apache-2.0
"""
from mkhvif import rect, header
from mktoolbaricons import Art, BLUE, EDGE, INK, PAPER, page

WOOD = (98, 66, 36, 255)
WOOD_LIT = (156, 110, 64, 255)
AMBER = ((122, 64, 10, 255), (230, 140, 40, 255), (250, 196, 120, 255))
GREEN = (67, 153, 72, 255)


def heading(art, x0, x1, y, h):
    """A heading's band across a page: dark under, lit over."""
    art.solid(BLUE[0], rect(x0, y, x1, y + h))
    art.solid(BLUE[1], rect(x0, y, x1 - 1, y + h - 1))


def doc_viewer(art):
    """Two sheets, the front one's heading in blue and paragraphs of grey
    under it: the application, as against one document."""
    page(art, 17, 1, 59, 55)
    page(art, 6, 7, 48, 62, [(27, 12, 42), (32, 12, 40), (37, 12, 42),
                             (45, 12, 41), (50, 12, 42), (55, 12, 33)])
    heading(art, 12, 34, 15, 7)


def doc_file(art):
    """One sheet of text under a blue heading: Tracker's document."""
    page(art, 12, 4, 52, 60, [(22, 17, 46), (27, 17, 44), (32, 17, 46),
                              (40, 17, 45), (45, 17, 46), (50, 17, 36)])
    heading(art, 17, 36, 11, 5)


def board(art, x0, y0, x1, y1):
    """A slide on a board: its edge, a white face, a chart on it."""
    art.solid(EDGE, rect(x0, y0, x1, y1))
    art.shaded((x0, y0), (x1, y1), PAPER, rect(x0 + 2, y0 + 2, x1 - 2,
                                               y1 - 2))
    w = (x1 - x0 - 12) / 4.0
    base = y1 - 5
    for i, (h, c) in enumerate(((0.35, BLUE[1]), (0.6, AMBER[1]),
                                (0.85, GREEN))):
        bx = x0 + 6 + i * (w + 2)
        art.solid(c, rect(bx, base - h * (y1 - y0 - 12), bx + w, base))
    art.solid(INK, rect(x0 + 5, base, x1 - 5, base + 1))


def slides_viewer(art):
    """An easel: three legs, and a board holding a chart."""
    art.solid(WOOD, [(30, 38), (34, 38), (35, 62), (29, 62)])
    art.solid(WOOD, [(12, 60), (22, 36), (26, 36), (17, 61)])
    art.solid(WOOD, [(52, 60), (42, 36), (38, 36), (47, 61)])
    art.solid(WOOD_LIT, [(13, 58), (22, 38), (23, 38), (15, 59)])
    board(art, 4, 6, 60, 42)
    art.solid(WOOD, rect(26, 3, 38, 8))


def slides_file(art):
    """A page with a slide on it: Tracker's presentation."""
    page(art, 12, 4, 52, 60, [(44, 17, 46), (49, 17, 40)])
    art.solid(AMBER[0], rect(16, 12, 48, 36))
    art.shaded((16, 12), (48, 36), [(0.0, AMBER[2]), (1.0, AMBER[1])],
               rect(17, 13, 47, 35))
    art.solid((255, 255, 255, 255), rect(20, 17, 38, 20))
    art.solid((255, 255, 255, 200), rect(20, 24, 44, 26))
    art.solid((255, 255, 255, 200), rect(20, 29, 40, 31))


ICONS = []
for name, draw in (("doc_viewer", doc_viewer), ("doc_file", doc_file),
                   ("slides_viewer", slides_viewer),
                   ("slides_file", slides_file)):
    a = Art()
    draw(a)
    ICONS.append((name, a.blob()))

print(header("tiku_icons_office.inl",
             "HVIF bytes for TikuDoc's and TikuSlides' icons and their files' "
             "icons: doc_viewer, doc_file, slides_viewer and slides_file; "
             "tiku_icons_tiku.c includes them.",
             "tools/icons/mkofficeicon.py"))
for name, blob in ICONS:
    print(f"static const uint8_t tiku_icon_{name}[] = {{")
    for at in range(0, len(blob), 12):
        print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at + 12]) + ",")
    print("};\n")
