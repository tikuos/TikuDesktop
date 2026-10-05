"""Emit the Editor's HVIF icon on the toolkit's 64-unit canvas.

A sheet with a folded corner, lines of text, and a pencil laid across it
from the lower right, readable at 14 px.
SPDX-License-Identifier: Apache-2.0
"""
from mkhvif import icon, path_polygon, shape, style_solid, rect, header


layers = []


def add(color, points):
    layers.append((style_solid(color), path_polygon(points)))


# The sheet: a shadow, the outline, the paper, and the folded corner.
add((30, 39, 50, 55), [(14, 62), (50, 62), (50, 60), (14, 60)])
add((70, 66, 58, 255), [(10, 4), (40, 4), (52, 16), (52, 60), (10, 60)])
add((255, 252, 236, 255), [(12, 6), (39, 6), (50, 17), (50, 58), (12, 58)])
add((70, 66, 58, 255), [(39, 4), (39, 17), (52, 17)])
add((226, 220, 196, 255), [(40, 7), (40, 16), (49, 16)])
# Lines of text.
for y in (22, 29, 36, 43):
    add((150, 150, 140, 255), rect(17, y, 45 if y < 43 else 34, y + 2))
# The pencil: shadow, the yellow shaft, the wood and the lead, laid
# diagonally from the lower right toward the middle.
add((30, 39, 50, 70), [(30, 46), (56, 20), (62, 26), (36, 52)])
add((80, 60, 20, 255), [(27, 43), (53, 17), (61, 25), (35, 51), (25, 53)])
add((243, 184, 57, 255), [(31, 43), (53, 21), (57, 25), (35, 47)])
add((255, 222, 119, 255), [(31, 43), (53, 21), (55, 23), (33, 45)])
add((222, 196, 150, 255), [(35, 47), (31, 43), (27, 51)])
add((60, 60, 60, 255), [(29, 49), (27, 51), (28, 52)])
add((200, 80, 90, 255), [(53, 21), (57, 25), (61, 25), (57, 17)])

blob = icon([s for s, _ in layers], [p for _, p in layers],
            [shape(i, i) for i in range(len(layers))])
print(header("tiku_icons_editor.inl",
             "the Editor's icon, a sheet of text with a pencil across it, as "
             "HVIF bytes that tiku_icons_tiku.c includes.",
             "tools/icons/mkeditoricon.py"))
print("static const uint8_t tiku_icon_editor[] = {")
for at in range(0, len(blob), 12):
    print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at+12]) + ",")
print("};")
