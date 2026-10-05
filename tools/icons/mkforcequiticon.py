"""Emit Force Quit's stop-sign HVIF icon on the toolkit's 64-unit canvas.

Dark outline, bevelled red face and a clear ivory cross, readable at 14 px.
SPDX-License-Identifier: Apache-2.0
"""
from mkhvif import icon, path_polygon, shape, style_solid, header


layers = [
    ((64, 36, 34, 255), [(20, 4), (44, 4), (60, 20), (60, 44),
                        (44, 60), (20, 60), (4, 44), (4, 20)]),
    ((188, 54, 44, 255), [(21, 8), (43, 8), (56, 21), (56, 43),
                         (43, 56), (21, 56), (8, 43), (8, 21)]),
    ((239, 123, 99, 255), [(21, 8), (43, 8), (56, 21), (53, 23),
                          (41, 12), (23, 12), (12, 23), (12, 41),
                          (8, 43), (8, 21)]),
    ((255, 249, 230, 255), [(23, 18), (32, 27), (41, 18), (46, 23),
                           (37, 32), (46, 41), (41, 46), (32, 37),
                           (23, 46), (18, 41), (27, 32), (18, 23)]),
]
blob = icon([style_solid(c) for c, _ in layers],
            [path_polygon(p) for _, p in layers],
            [shape(i, i) for i in range(len(layers))])
print(header("tiku_icons_forcequit.inl",
             "Force Quit's icon, a red stop sign with an ivory cross, as HVIF "
             "bytes that tiku_icons_tiku.c includes.",
             "tools/icons/mkforcequiticon.py"))
print("static const uint8_t tiku_icon_force_quit[] = {")
for at in range(0, len(blob), 12):
    print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at+12]) + ",")
print("};")
