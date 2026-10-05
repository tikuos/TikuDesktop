"""Emit the HVIF artwork of TikuDiff and TikuGrab as C arrays.

TikuDiff: its application icon, two pages with a line taken out of one
and put into the other; toolbar buttons to compare two files and two
folders, and to swap the sides.  TikuGrab: a camera for the application,
and buttons for the whole desk, one window, and the timer.  Drawn as the
rest of the set: a dark edge, faces shaded from the lit top left, a
highlight.  Run from tools/icons, into kits/translation/tiku_icons_utils.inl.
SPDX-License-Identifier: Apache-2.0
"""
import math
from mkhvif import circle, ring, rect, header
from mktoolbaricons import Art, BLUE, EDGE, page

RED_BAR = (206, 66, 58, 255)
GREEN_BAR = (70, 160, 80, 255)
FOLDER = ((104, 78, 28, 255), [(0.0, (252, 222, 128, 255)),
                               (1.0, (222, 168, 58, 255))])
DARK = (54, 58, 66, 255)
BODY = [(0.0, (120, 126, 138, 255)), (1.0, (62, 66, 76, 255))]
GLASS = [(0.0, (190, 222, 250, 255)), (1.0, (40, 76, 130, 255))]
DESK = [(0.0, (98, 150, 206, 255)), (1.0, (44, 86, 140, 255))]
TAB = (250, 204, 40, 255)
PANEL = [(0.0, (244, 244, 246, 255)), (1.0, (206, 210, 218, 255))]


def two_pages(art):
    """TikuDiff: a page behind another, a red line on the one and a green
    line on the other, what a comparison shows."""
    page(art, 4, 4, 38, 52, [(14, 10, 30), (24, 10, 28), (34, 10, 30)])
    art.solid(RED_BAR, rect(9, 18.5, 32, 22.5))
    page(art, 24, 12, 60, 60, [(22, 30, 52), (32, 30, 50), (50, 30, 50)])
    art.solid(GREEN_BAR, rect(29, 38.5, 54, 42.5))


def compare_files(art):
    """Two pages side by side: one line red, its twin green."""
    page(art, 3, 8, 30, 58, [(18, 8, 24), (38, 8, 24), (46, 8, 22)])
    art.solid(RED_BAR, rect(7, 27, 26, 31))
    page(art, 34, 8, 61, 58, [(18, 39, 55), (38, 39, 55), (46, 39, 53)])
    art.solid(GREEN_BAR, rect(38, 27, 57, 31))


def folder(art, x0, y0, x1, y1):
    """A folder: its tab, its face lit from the top left."""
    edge, face = FOLDER
    art.solid(edge, [(x0, y0 + 4), (x0 + 3, y0), (x0 + 12, y0),
                     (x0 + 15, y0 + 4), (x1, y0 + 4), (x1, y1), (x0, y1)])
    art.shaded((x0, y0), (x1, y1), face,
               [(x0 + 2, y0 + 6), (x1 - 2, y0 + 6), (x1 - 2, y1 - 2),
                (x0 + 2, y1 - 2)])
    art.solid((255, 240, 190, 255), rect(x0 + 2, y0 + 6, x1 - 2, y0 + 8))


def compare_folders(art):
    """Two folders side by side."""
    folder(art, 2, 16, 30, 50)
    folder(art, 34, 16, 62, 50)


# An arrow pointing right: its edge, its face inset, the light along it.
RIGHT = [(6, 13), (40, 13), (40, 4), (58, 19), (40, 34), (40, 25), (6, 25)]
RIGHT_IN = [(8, 15), (42, 15), (42, 9), (54, 19), (42, 29), (42, 23),
            (8, 23)]
RIGHT_LIT = [(8, 15), (42, 15), (42, 9), (46, 12), (44, 12), (44, 18),
             (8, 18)]


def swap_arrows(art):
    """An arrow going right above one coming back: the sides turned."""
    for dy, flip in ((0, False), (26, True)):
        for colour, pts in zip(BLUE, (RIGHT, RIGHT_IN, RIGHT_LIT)):
            art.solid(colour, [(64 - x if flip else x, y + dy)
                               for x, y in pts])


def camera(art):
    """TikuGrab: a camera, its lens lit, a flash on its shoulder."""
    art.solid(DARK, [(8, 18), (22, 18), (26, 11), (40, 11), (44, 18),
                     (56, 18), (60, 22), (60, 52), (56, 56), (8, 56),
                     (4, 52), (4, 22)])
    art.shaded((6, 20), (58, 54), BODY,
               [(8, 21), (56, 21), (57, 23), (57, 51), (55, 53), (9, 53),
                (7, 51), (7, 23)])
    art.solid((226, 230, 236, 255), rect(46, 24, 54, 29))
    art.solid(DARK, circle(32, 37, 14, 32))
    art.shaded((20, 25), (44, 49), GLASS, circle(32, 37, 11, 32))
    art.solid((150, 156, 166, 255), ring(32, 37, 14, 12, 32))
    art.solid((255, 255, 255, 190), [(24, 33), (27, 28), (32, 26), (31, 29),
                                     (28, 31), (26, 35)])


def screen(art):
    """The whole desk: a screen with the desk's blue on it, on a stand."""
    art.solid(DARK, rect(4, 8, 60, 48))
    art.shaded((7, 11), (57, 45), DESK, rect(7, 11, 57, 45))
    art.solid(TAB, rect(14, 16, 30, 21))
    art.shaded((14, 21), (44, 40), PANEL, rect(14, 21, 44, 40))
    art.solid(DARK, [(26, 48), (38, 48), (40, 56), (24, 56)])
    art.solid(DARK, rect(18, 55, 46, 59))


def window(art):
    """One window as the desk draws it: its yellow tab, its grey page."""
    art.solid(EDGE, [(4, 12), (6, 8), (32, 8), (34, 12), (34, 16), (60, 16),
                     (60, 56), (4, 56)])
    art.solid(TAB, [(6, 10), (32, 10), (32, 17), (6, 17)])
    art.shaded((6, 18), (58, 54), PANEL, rect(6, 18, 58, 54))
    art.solid((150, 156, 166, 255), rect(12, 26, 50, 29))
    art.solid((150, 156, 166, 255), rect(12, 34, 44, 37))
    art.solid((150, 156, 166, 255), rect(12, 42, 48, 45))


def timer(art):
    """A stopwatch: its crown, its face, a hand at a few seconds."""
    art.solid(DARK, rect(27, 3, 37, 9))
    art.solid(DARK, rect(30, 8, 34, 13))
    art.solid(DARK, circle(32, 36, 24, 36))
    art.shaded((12, 16), (52, 56), [(0.0, (255, 255, 255, 255)),
                                    (1.0, (208, 214, 224, 255))],
               circle(32, 36, 20, 36))
    for k in range(12):
        a = math.radians(k * 30)
        x, y = 32 + 16 * math.sin(a), 36 - 16 * math.cos(a)
        art.solid(DARK, rect(x - 1, y - 1, x + 1, y + 1))
    art.solid(RED_BAR, [(31, 36), (33, 36), (44, 25), (43, 24)])
    art.solid(DARK, circle(32, 36, 3, 12))


ICONS = []
for name, draw in (("diff_viewer", two_pages),
                   ("compare_files", compare_files),
                   ("compare_folders", compare_folders),
                   ("swap_sides", swap_arrows),
                   ("screen_grab", camera),
                   ("grab_desk", screen),
                   ("grab_window", window),
                   ("grab_timer", timer)):
    a = Art()
    draw(a)
    ICONS.append((name, a.blob()))

print(header("tiku_icons_utils.inl",
             "HVIF bytes for TikuDiff's icon and toolbar buttons "
             "(diff_viewer, compare_files, compare_folders, swap_sides) and "
             "TikuGrab's (screen_grab, grab_desk, grab_window, grab_timer); "
             "tiku_icons_tiku.c includes them.",
             "tools/icons/mkutilicons.py"))
for name, blob in ICONS:
    print(f"static const uint8_t tiku_icon_{name}[] = {{")
    for at in range(0, len(blob), 12):
        print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at + 12]) + ",")
    print("};\n")
