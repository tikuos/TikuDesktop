"""Emit HVIF icons for the programming tools, the notes, the developer
calculator and the demos on the toolkit's 64-unit canvas.

Each is one mark readable at 14 px: a sheet with a BASIC line number and
prompt, a sheet with a C brace, a slate calculator with a hex readout,
a yellow sticky note, a waving hand, a clock face, a counter dial, a
pencil stroke on a canvas, a thermometer, and two overlapping windows.
SPDX-License-Identifier: Apache-2.0
"""
import sys
from mkhvif import icon, path_polygon, shape, style_solid, rect, circle, ring

DARK = (28, 31, 38, 255)
SLATE = (70, 76, 88, 255)
LIT = (104, 112, 124, 255)
PAPER = (255, 252, 236, 255)
INK = (150, 150, 140, 255)
ORANGE = (232, 140, 48, 255)
BLUE = (74, 141, 198, 255)
GREEN = (92, 230, 120, 255)
NAVY = (33, 48, 69, 255)


def sheet(layers):
    layers.append(((30, 39, 50, 55), rect(14, 60, 50, 62)))
    layers.append(((70, 66, 58, 255), [(10, 4), (40, 4), (52, 16), (52, 60), (10, 60)]))
    layers.append((PAPER, [(12, 6), (39, 6), (50, 17), (50, 58), (12, 58)]))
    layers.append(((70, 66, 58, 255), [(39, 4), (39, 17), (52, 17)]))
    layers.append(((226, 220, 196, 255), [(40, 7), (40, 16), (49, 16)]))


def digit_1(layers, x, y, c):
    layers.append((c, rect(x + 3, y, x + 6, y + 12)))
    layers.append((c, [(x, y + 3), (x + 6, y - 1), (x + 6, y + 3), (x + 2, y + 5)]))


def digit_0(layers, x, y, c):
    layers.append((c, [(x, y), (x + 8, y), (x + 8, y + 12), (x, y + 12), (x, y),
                       (x + 3, y + 3), (x + 3, y + 9), (x + 5, y + 9), (x + 5, y + 3),
                       (x + 3, y + 3)]))


def basic():
    layers = []
    sheet(layers)
    # "10" in navy, then a line of code in ink, twice
    for row, y in enumerate((22, 40)):
        digit_1(layers, 15, y, NAVY)
        digit_0(layers, 23, y, NAVY)
        layers.append((INK, rect(35, y + 3, 46, y + 6)))
        layers.append((INK, rect(35, y + 8, 43 if row else 46, y + 11)))
    layers.append((ORANGE, [(15, 56), (15, 44), (21, 50)]))
    return layers


def c_lang():
    layers = []
    sheet(layers)
    # a brace, and lines of code
    layers.append((NAVY, [(24, 18), (30, 18), (30, 22), (27, 22), (27, 33),
                          (23, 36), (27, 39), (27, 50), (30, 50), (30, 54),
                          (24, 54), (23, 52), (23, 41), (19, 36), (23, 31),
                          (23, 20)]))
    for y in (24, 31, 38, 45):
        layers.append((INK, rect(33, y, 46 if y % 2 else 42, y + 3)))
    return layers


def devcalc():
    layers = []
    layers.append((DARK, [(10, 2), (54, 2), (60, 8), (60, 58), (54, 62), (10, 62), (4, 58), (4, 8)]))
    layers.append(((92, 100, 112, 255), [(11, 5), (53, 5), (57, 9), (57, 55), (53, 59), (11, 59), (7, 55), (7, 9)]))
    layers.append(((58, 64, 74, 255), [(13, 8), (55, 8), (57, 10), (57, 55), (53, 59), (13, 59), (10, 56), (10, 11)]))
    layers.append(((22, 26, 30, 255), rect(11, 9, 53, 23)))
    layers.append(((14, 20, 18, 255), rect(12, 10, 52, 22)))
    # "0x" glowing green in the display
    digit_0(layers, 15, 11, GREEN)
    layers.append((GREEN, [(26, 12), (29, 12), (35, 22), (32, 22)]))
    layers.append((GREEN, [(32, 12), (35, 12), (29, 22), (26, 22)]))
    layers.append((GREEN, rect(40, 12, 49, 21)))
    # a bit grid: eight cells, some lit
    for i in range(8):
        for row in range(3):
            x, y = 12 + i * 5, 27 + row * 10
            lit = (i + row) % 3 == 0
            layers.append((DARK, rect(x, y, x + 4, y + 8)))
            layers.append((ORANGE if lit else (238, 234, 222, 255), rect(x, y, x + 3, y + 7)))
    return layers


def notes():
    layers = []
    layers.append(((30, 39, 50, 70), [(12, 60), (56, 58), (58, 62), (14, 63)]))
    layers.append(((150, 130, 30, 255), [(8, 6), (56, 6), (56, 56), (48, 60), (8, 58)]))
    layers.append(((255, 230, 100, 255), [(10, 8), (54, 8), (54, 54), (47, 58), (10, 56)]))
    layers.append(((255, 246, 160, 255), rect(10, 8, 54, 14)))
    layers.append(((240, 200, 60, 255), [(47, 58), (54, 54), (46, 50)]))
    for y in (22, 30, 38, 46):
        layers.append(((120, 100, 40, 255), rect(15, y, 44 if y < 46 else 32, y + 2)))
    return layers


def hello():
    layers = []
    # a speech balloon with "Hi"
    layers.append((NAVY, [(6, 8), (58, 8), (58, 44), (30, 44), (18, 56), (20, 44), (6, 44)]))
    layers.append((PAPER, [(9, 11), (55, 11), (55, 41), (28, 41), (22, 49), (23, 41), (9, 41)]))
    layers.append((NAVY, rect(16, 18, 20, 34)))
    layers.append((NAVY, rect(28, 18, 32, 34)))
    layers.append((NAVY, rect(20, 24, 28, 28)))
    layers.append((ORANGE, rect(37, 24, 44, 34)))
    layers.append((ORANGE, rect(37, 17, 44, 22)))
    return layers


def clock():
    layers = []
    layers.append((DARK, circle(32, 32, 28)))
    layers.append((LIT, circle(32, 32, 25)))
    layers.append((PAPER, circle(32, 32, 22)))
    for cx, cy in ((32, 13), (51, 32), (32, 51), (13, 32)):
        layers.append((DARK, rect(cx - 1, cy - 1, cx + 2, cy + 2)))
    layers.append((DARK, [(30, 33), (34, 33), (34, 17), (30, 17)]))
    layers.append((DARK, [(31, 30), (31, 34), (47, 36), (47, 32)]))
    layers.append((ORANGE, circle(32, 32, 3, 12)))
    return layers


def counter():
    layers = []
    layers.append((DARK, [(8, 14), (56, 14), (60, 18), (60, 46), (56, 50), (8, 50), (4, 46), (4, 18)]))
    layers.append((SLATE, [(9, 17), (55, 17), (57, 19), (57, 45), (55, 47), (9, 47), (7, 45), (7, 19)]))
    for i, d in enumerate((0, 4, 2)):
        x = 11 + i * 14
        layers.append(((22, 26, 30, 255), rect(x, 20, x + 14, 44)))
        layers.append((PAPER, rect(x + 1, 21, x + 13, 43)))
        if d == 0:
            digit_0(layers, x + 2, 26, DARK)
        elif d == 4:
            layers.append((DARK, rect(x + 7, 26, x + 10, 38)))
            layers.append((DARK, [(x + 2, 26), (x + 5, 26), (x + 2, 34), (x + 2, 34)]))
            layers.append((DARK, rect(x + 2, 32, x + 10, 35)))
            layers.append((DARK, rect(x + 2, 26, x + 5, 34)))
        else:
            layers.append((DARK, rect(x + 2, 26, x + 10, 29)))
            layers.append((DARK, rect(x + 7, 26, x + 10, 33)))
            layers.append((DARK, rect(x + 2, 31, x + 10, 34)))
            layers.append((DARK, rect(x + 2, 33, x + 5, 38)))
            layers.append((DARK, rect(x + 2, 35, x + 10, 38)))
    layers.append((ORANGE, [(28, 52), (36, 52), (32, 58)]))
    return layers


def sketch():
    layers = []
    layers.append(((70, 66, 58, 255), rect(4, 8, 60, 56)))
    layers.append((PAPER, rect(7, 11, 57, 53)))
    # a blue stroke curving across the canvas
    stroke = [(12, 44), (18, 30), (26, 24), (34, 30), (40, 40), (48, 36), (52, 20)]
    for (x0, y0), (x1, y1) in zip(stroke, stroke[1:]):
        layers.append((BLUE, [(x0 - 3, y0), (x1 - 3, y1), (x1 + 3, y1), (x0 + 3, y0)]))
    layers.append((ORANGE, circle(52, 20, 5, 12)))
    return layers


def sensors():
    layers = []
    # a thermometer: the tube, the bulb, the mercury
    layers.append((DARK, [(26, 4), (38, 4), (38, 40), (44, 46), (44, 56), (36, 62), (28, 62), (20, 56), (20, 46), (26, 40)]))
    layers.append((PAPER, [(28, 7), (36, 7), (36, 41), (41, 47), (41, 54), (35, 59), (29, 59), (23, 54), (23, 47), (28, 41)]))
    layers.append(((214, 60, 50, 255), rect(30, 22, 34, 44)))
    layers.append(((214, 60, 50, 255), circle(32, 51, 7, 16)))
    for y in (12, 18, 24, 30, 36):
        layers.append((INK, rect(38, y, 44, y + 2)))
    return layers


def twowin():
    layers = []
    for dx, dy, face in ((4, 6, (200, 205, 210, 255)), (18, 20, PAPER)):
        layers.append((DARK, rect(dx, dy, dx + 42, dy + 36)))
        layers.append((face, rect(dx + 2, dy + 2, dx + 40, dy + 34)))
        layers.append((ORANGE, rect(dx + 2, dy + 2, dx + 22, dy + 8)))
    return layers


ICONS = {"basic": basic, "c": c_lang, "devcalc": devcalc, "notes": notes,
         "hello": hello, "clock": clock, "counter": counter,
         "sketch": sketch, "sensors": sensors, "twowin": twowin}
name = sys.argv[1]
layers = ICONS[name]()
blob = icon([style_solid(c) for c, _ in layers],
            [path_polygon(p) for _, p in layers],
            [shape(i, i) for i in range(len(layers))])
print("/* Generated by TikuTracker/tools/mkmoreicons.py %s.\n"
      " * Authored TikuOS vector artwork; SPDX-License-Identifier: Apache-2.0 */" % name)
print("static const uint8_t tiku_icon_app_%s[] = {" % name)
for at in range(0, len(blob), 12):
    print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at+12]) + ",")
print("};")
