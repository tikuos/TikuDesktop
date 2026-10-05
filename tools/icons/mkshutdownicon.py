"""Emit Shut down's power-mark HVIF icon on the toolkit's 64-unit canvas.

A dark slate button with a lit rim and the ivory power sign: a ring open
at the top with the bar standing in the gap, readable at 14 px.
SPDX-License-Identifier: Apache-2.0
"""
import math
from mkhvif import icon, path_polygon, shape, style_solid, circle, rect, header


def arc_ring(cx, cy, ro, ri, start, end, n=14):
    """A ring between two angles (degrees, y down): outer one way, inner back."""
    outer = [(cx + ro * math.cos(math.radians(start + (end - start) * i / n)),
              cy + ro * math.sin(math.radians(start + (end - start) * i / n)))
             for i in range(n + 1)]
    inner = [(cx + ri * math.cos(math.radians(start + (end - start) * i / n)),
              cy + ri * math.sin(math.radians(start + (end - start) * i / n)))
             for i in range(n, -1, -1)]
    return [(round(x, 1), round(y, 1)) for x, y in outer + inner]


layers = [
    ((40, 42, 48, 255), circle(32, 32, 28)),
    ((110, 118, 130, 255), circle(32, 32, 24)),
    ((72, 78, 88, 255), circle(34, 34, 23)),
    ((255, 249, 230, 255), arc_ring(32, 34, 15, 8, -55, 235)),
    ((255, 249, 230, 255), rect(28, 14, 36, 34)),
]
blob = icon([style_solid(c) for c, _ in layers],
            [path_polygon(p) for _, p in layers],
            [shape(i, i) for i in range(len(layers))])
print(header("tiku_icons_shutdown.inl",
             "Shut down's icon, a slate button with the power sign, as HVIF "
             "bytes that tiku_icons_tiku.c includes.",
             "tools/icons/mkshutdownicon.py"))
print("static const uint8_t tiku_icon_shutdown[] = {")
for at in range(0, len(blob), 12):
    print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at+12]) + ",")
print("};")
