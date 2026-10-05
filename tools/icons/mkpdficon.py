"""Emit TikuPDF's document HVIF artwork as a C array.

An ivory page with its corner folded, a few lines of text, and a red
label across it carrying the letters, on the toolkit's 64-unit canvas.
SPDX-License-Identifier: Apache-2.0
"""
from mkhvif import icon, path_polygon, shape, style_solid, rect, header


layers = []


def add(color, points):
    layers.append((style_solid(color), path_polygon(points)))


# Shadow, the page's edge and its face, the fold at the top right.
add((30, 39, 50, 60), [(13, 8), (45, 8), (57, 20), (57, 63), (13, 63)])
add((62, 66, 76, 255), [(9, 3), (43, 3), (55, 15), (55, 60), (9, 60)])
add((251, 250, 244, 255), [(11, 5), (42, 5), (53, 16), (53, 58), (11, 58)])
add((206, 209, 216, 255), [(42, 5), (42, 16), (53, 16)])
add((150, 154, 164, 255), [(42, 5), (43, 5), (43, 15), (53, 15), (53, 16),
                           (42, 16)])
# Lines of text above the label and below it.
for y in (21, 26, 31):
    add((158, 162, 172, 255), rect(16, y, 47 if y != 31 else 38, y + 2))
add((158, 162, 172, 255), rect(16, 55, 40, 56))
# The label, tucked under the page's left edge, and its letters.
add((128, 22, 18, 255), [(5, 51), (10, 51), (10, 55)])
add((210, 44, 36, 255), rect(5, 36, 44, 51))
add((236, 88, 72, 255), rect(5, 36, 44, 38))
white = (255, 255, 255, 255)
# P
add(white, rect(9, 40, 11.5, 49))
add(white, rect(9, 40, 15, 42))
add(white, rect(9, 44, 15, 46))
add(white, rect(14, 40, 16.5, 46))
# D
add(white, rect(19, 40, 21.5, 49))
add(white, rect(19, 40, 25, 42))
add(white, rect(19, 47, 25, 49))
add(white, [(24.5, 40), (26, 40.8), (27.5, 42.5), (27.5, 46.5), (26, 48.2),
            (24.5, 49), (24.5, 47), (25, 46.5), (25, 42.5), (24.5, 42)])
# F
add(white, rect(30.5, 40, 33, 49))
add(white, rect(30.5, 40, 38, 42))
add(white, rect(30.5, 44, 36, 46))

blob = icon([s for s, _ in layers], [p for _, p in layers],
            [shape(i, i) for i in range(len(layers))])
print(header("tiku_icons_pdf.inl",
             "TikuPDF's icon, an ivory page with a red label across it, as "
             "HVIF bytes that tiku_icons_tiku.c includes.",
             "tools/icons/mkpdficon.py"))
print("static const uint8_t tiku_icon_pdf_viewer[] = {")
for at in range(0, len(blob), 12):
    print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at+12]) + ",")
print("};")
