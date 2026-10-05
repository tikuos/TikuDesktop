"""Emit the document toolbar's HVIF artwork as C arrays: page up and down,
zoom in and out, fit width and fit page, rotate left and right, find
previous and next, a document's outline, pages and signatures, and an
archive's extract-all and extract.

Drawn the way the rest of the set is: a dark edge, a face shaded from its
lit top-left, a highlight; the arrows are nav_back's own, turned.  Run from
tools/icons, into kits/translation/tiku_icons_toolbar.inl.
SPDX-License-Identifier: Apache-2.0
"""
import math
from mkhvif import (icon, path_polygon, shape, style_linear, style_solid,
                    circle, ring, rect, header)

# The palettes the set already speaks in.
GREEN = ((35, 49, 35, 255), (67, 153, 72, 255), (164, 215, 142, 255))
BLUE = ((37, 64, 95, 255), (63, 133, 197, 255), (163, 208, 239, 255))
EDGE = (70, 74, 84, 255)
PAPER = [(0.0, (255, 255, 255, 255)), (1.0, (214, 219, 228, 255))]
INK = (140, 146, 158, 255)
RIM = (0x6E, 0x76, 0x80, 0xFF)
GRIP = (0x5A, 0x4A, 0x36, 0xFF)
GRIP_LIT = (0x8C, 0x74, 0x54, 0xFF)
RED = ((104, 16, 16, 255), (150, 26, 24, 255), (196, 44, 38, 255))


class Art:
    """Layers painted in order, each a style and one path."""
    def __init__(self):
        self.styles, self.paths = [], []

    def solid(self, rgba, points):
        self.styles.append(style_solid(rgba))
        self.paths.append(path_polygon(points))

    def shaded(self, p0, p1, stops, points):
        self.styles.append(style_linear(p0, p1, stops))
        self.paths.append(path_polygon(points))

    def blob(self):
        return icon(self.styles, self.paths,
                    [shape(i, i) for i in range(len(self.paths))])


def page(art, x0, y0, x1, y1, lines=()):
    """A sheet: its edge, its face lit from the top left, the corner turned
    down, and a few grey lines of text at the heights given."""
    f = min(8, (x1 - x0) // 3)
    art.solid(EDGE, [(x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1),
                     (x0, y1)])
    art.shaded((x0, y0), (x1, y1), PAPER,
               [(x0 + 2, y0 + 2), (x1 - f - 1, y0 + 2), (x1 - 2, y0 + f + 1),
                (x1 - 2, y1 - 2), (x0 + 2, y1 - 2)])
    art.solid((196, 201, 212, 255), [(x1 - f - 1, y0 + 2),
                                     (x1 - f - 1, y0 + f + 1),
                                     (x1 - 2, y0 + f + 1)])
    for y, xa, xb in lines:
        art.solid(INK, rect(xa, y, xb, y + 3))


# nav_back's arrow, pointing left, its tip at the left edge.
ARROW = [(6, 31), (29, 8), (29, 22), (57, 22), (57, 41), (29, 41), (29, 54)]
INNER = [(10, 31), (26, 15), (26, 25), (54, 25), (54, 38), (26, 38), (26, 47)]
SHINE = [(10, 31), (26, 15), (26, 25), (54, 25), (54, 29), (23, 29),
         (23, 24), (14, 33)]


def arrow(art, colours, turn, scale=1.0, at=(32, 32)):
    """nav_back's arrow turned to point up ('up'), down, or left, scaled
    about the canvas's centre and moved to @at."""
    def place(points):
        out = []
        for x, y in points:
            if turn == "up":
                x, y = y, x
            elif turn == "down":
                x, y = y, 64 - x
            out.append((at[0] + (x - 32) * scale, at[1] + (y - 32) * scale))
        return out
    for colour, points in zip(colours, (ARROW, INNER, SHINE)):
        art.solid(colour, place(points))


def double_arrow(art, vertical):
    """A blue arrow pointing both ways across the canvas: along it (fit
    width), or up and down (fit page)."""
    outer = [(3, 32), (16, 19), (16, 27), (48, 27), (48, 19), (61, 32),
             (48, 45), (48, 37), (16, 37), (16, 45)]
    inner = [(7, 32), (14, 25), (14, 30), (50, 30), (50, 25), (57, 32),
             (50, 39), (50, 34), (14, 34), (14, 39)]
    shine = [(8, 31), (14, 25), (14, 30), (50, 30), (50, 25), (56, 31),
             (50, 31), (50, 32), (14, 32), (14, 31)]
    for colour, points in zip(BLUE, (outer, inner, shine)):
        art.solid(colour, [(y, x) for x, y in points] if vertical else points)


def magnifier(art, sign):
    """The set's magnifier, its glass lit, a plus or a minus in it."""
    art.solid(GRIP, [(35, 41), (41, 35), (59, 53), (53, 59)])
    art.solid(GRIP_LIT, [(37, 39), (39, 37), (57, 55), (55, 57)])
    art.shaded((10, 10), (42, 42), [(0.0, (246, 250, 253, 255)),
                                    (1.0, (170, 202, 230, 255))],
               circle(26, 26, 17, 32))
    art.solid(RIM, ring(26, 26, 19, 15.5, 32))
    art.solid((255, 255, 255, 170), [(15, 23), (17, 18), (21, 14), (25, 12),
                                     (24, 15), (20, 18), (18, 22)])
    dark = (30, 52, 86, 255)
    art.solid(dark, rect(16, 23.5, 36, 28.5))
    if sign == "+":
        art.solid(dark, rect(23.5, 16, 28.5, 36))


def turn_arrow(art, clockwise):
    """A blue arrow curving over the top of a page: clockwise to the right,
    or the other way."""
    page(art, 20, 27, 44, 58, [(38, 25, 39), (43, 25, 39), (48, 25, 35)])
    cx, cy, ro, ri = 32, 42, 29, 19
    a0, a1, n = math.radians(190), math.radians(335), 24

    def arc(r0, r1, pad=0.0):
        angles = [a0 + pad + (a1 - a0 - 2 * pad) * i / n for i in range(n + 1)]
        return ([(cx + r0 * math.cos(a), cy + r0 * math.sin(a))
                 for a in angles] +
                [(cx + r1 * math.cos(a), cy + r1 * math.sin(a))
                 for a in reversed(angles)])

    def head(r0, r1, grow):
        base, tip = a1 - math.radians(2), a1 + math.radians(28)
        return [(cx + (r0 - grow) * math.cos(base),
                 cy + (r0 - grow) * math.sin(base)),
                (cx + (r1 + grow) * math.cos(base),
                 cy + (r1 + grow) * math.sin(base)),
                (cx + (r0 + r1) / 2 * math.cos(tip),
                 cy + (r0 + r1) / 2 * math.sin(tip))]
    layers = [(BLUE[0], arc(ro, ri), head(ri, ro, 7)),
              (BLUE[1], arc(ro - 2, ri + 2, 0.03), head(ri + 2, ro - 2, 5)),
              (BLUE[2], arc(ro - 2, ro - 5, 0.05), None)]
    for colour, band, tip in layers:
        for points in (band, tip):
            if points is None:
                continue
            art.solid(colour, points if clockwise
                      else [(64 - x, y) for x, y in points])


def outline_icon(art):
    """A page of contents: entries, some under others."""
    page(art, 10, 4, 54, 60)
    for y, indent, end in ((14, 0, 46), (24, 7, 46), (34, 7, 42),
                           (44, 0, 46)):
        art.solid(BLUE[0], rect(15 + indent, y, 21 + indent, y + 6))
        art.solid(BLUE[2], rect(16 + indent, y + 1, 19 + indent, y + 3))
        art.solid(INK, rect(24 + indent, y + 1.5, end, y + 4.5))


def pages_icon(art):
    """Four pages drawn small, two by two."""
    for x0, y0 in ((6, 3), (34, 3), (6, 34), (34, 34)):
        page(art, x0, y0, x0 + 24, y0 + 28,
             [(y0 + 9, x0 + 5, x0 + 17), (y0 + 15, x0 + 5, x0 + 19),
              (y0 + 21, x0 + 5, x0 + 15)])


def seal_icon(art):
    """A red seal on two ribbons, a tick in it: signed."""
    for tail in ([(20, 36), (31, 42), (27, 62), (22, 56), (15, 60)],
                 [(33, 42), (44, 36), (49, 60), (42, 56), (37, 62)]):
        art.solid(RED[0], tail)
        art.solid(RED[1], [(x + (1 if x < 32 else -1), y + 1)
                           for x, y in tail[:3]])
    art.solid(RED[0], circle(32, 25, 21, 36))
    art.shaded((14, 7), (50, 43), [(0.0, (246, 118, 104, 255)),
                                   (1.0, (170, 30, 28, 255))],
               circle(32, 25, 19, 36))
    art.solid((252, 170, 160, 255), ring(32, 25, 14.5, 12.5, 36))
    art.solid((255, 255, 255, 255), [(22, 25), (26, 21), (30, 25), (40, 15),
                                     (44, 19), (30, 33)])


BOX = ((84, 56, 28, 255), [(0.0, (222, 174, 108, 255)),
                           (1.0, (172, 120, 62, 255))],
       (232, 196, 140, 255), (150, 102, 52, 255))


def open_box(art, x0, y0, x1, y1):
    """A cardboard box seen from the front, its flaps open: the inside
    dark between them, the front shaded."""
    edge, face, lit, side = BOX
    fl = (y1 - y0) // 3
    art.solid(edge, [(x0 - 5, y0 - fl), (x0 + 4, y0), (x0, y0)])
    art.solid(lit, [(x0 - 3, y0 - fl + 2), (x0 + 1, y0 - 1), (x0 + 1, y0 - 3)])
    art.solid(edge, [(x1 + 5, y0 - fl), (x1 - 4, y0), (x1, y0)])
    art.solid(lit, [(x1 + 3, y0 - fl + 2), (x1 - 1, y0 - 1), (x1 - 1, y0 - 3)])
    art.solid((60, 40, 20, 255), [(x0, y0 - 3), (x1, y0 - 3), (x1, y0 + 2),
                                   (x0, y0 + 2)])
    art.solid(edge, rect(x0, y0, x1, y1))
    art.shaded((x0, y0), (x1, y1), face, rect(x0 + 2, y0 + 2, x1 - 2, y1 - 2))
    art.solid(side, rect(x0 + 2, y1 - 5, x1 - 2, y1 - 2))


def extract_all_icon(art):
    """Everything out: a green arrow rising from an open box."""
    open_box(art, 12, 36, 52, 60)
    arrow(art, GREEN, "up", 0.62, at=(32, 22))


def extract_some_icon(art):
    """The chosen ones out: a green arrow rising from a page."""
    page(art, 12, 22, 44, 60, [(34, 18, 34), (40, 18, 38), (46, 18, 32),
                              (52, 18, 36)])
    arrow(art, GREEN, "up", 0.5, at=(44, 18))


ICONS = []


def make(name, draw):
    art = Art()
    draw(art)
    ICONS.append((name, art.blob()))


make("page_up", lambda a: arrow(a, GREEN, "up"))
make("page_down", lambda a: arrow(a, GREEN, "down"))
make("find_prev", lambda a: arrow(a, BLUE, "up", 0.78))
make("find_next", lambda a: arrow(a, BLUE, "down", 0.78))
make("zoom_in", lambda a: magnifier(a, "+"))
make("zoom_out", lambda a: magnifier(a, "-"))
make("fit_width", lambda a: (page(a, 17, 4, 47, 60, [(12, 22, 38),
                                                    (48, 22, 36)]),
                             double_arrow(a, False)))
make("fit_page", lambda a: (page(a, 13, 8, 51, 56, [(16, 18, 34),
                                                   (22, 18, 30),
                                                   (44, 18, 32)]),
                            double_arrow(a, True)))
make("rotate_right", lambda a: turn_arrow(a, True))
make("rotate_left", lambda a: turn_arrow(a, False))
make("doc_outline", outline_icon)
make("doc_pages", pages_icon)
make("doc_signed", seal_icon)
make("extract_all", extract_all_icon)
make("extract_some", extract_some_icon)

if __name__ == "__main__":
    print(header("tiku_icons_toolbar.inl",
                 "HVIF bytes for the document toolbar's buttons: page up and "
                 "down, find previous and next, zoom in and out, fit width "
                 "and fit page, rotate right and left, a document's outline, "
                 "pages and signatures, and an archive's extract all and "
                 "extract; tiku_icons_tiku.c includes them.",
                 "tools/icons/mktoolbaricons.py"))
    for name, blob in ICONS:
        print(f"static const uint8_t tiku_icon_{name}[] = {{")
        for at in range(0, len(blob), 12):
            print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at + 12])
                  + ",")
        print("};\n")
