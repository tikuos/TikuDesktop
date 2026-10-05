"""Emit the Devices Preferences sections' HVIF icons on the 64-unit canvas.

Eight marks, one a section of what a board reports, all readable at
16 px beside their words in a source list: an identity tag, a processor
chip, a memory stick, a storage stack, a power bolt, a radio mast, a
security padlock and a boot ring.
SPDX-License-Identifier: Apache-2.0
"""
import sys
from mkhvif import (icon, path_polygon, shape, style_solid, rect, circle,
                    ring, header)

DARK = (28, 31, 38, 255)
SLATE = (70, 76, 88, 255)
SILVER = (180, 184, 190, 255)
PAPER = (246, 244, 236, 255)
BLUE = (59, 110, 165, 255)
GREEN = (46, 139, 87, 255)
AMBER = (232, 160, 48, 255)
CARMINE = (176, 58, 72, 255)
VIOLET = (106, 76, 147, 255)
OLIVE = (138, 138, 46, 255)


def identity():
    """A name tag: a paper card with a hole, a face dot and two lines."""
    layers = [(SLATE, [(10, 12), (54, 12), (58, 16), (58, 50), (54, 54),
                       (10, 54), (6, 50), (6, 16)]),
              (PAPER, [(12, 14), (52, 14), (56, 18), (56, 48), (52, 52),
                       (12, 52), (8, 48), (8, 18)]),
              (SLATE, circle(32, 10, 4, 16)),
              (BLUE, circle(21, 30, 7, 20)),
              (SLATE, rect(32, 25, 50, 29)), (SLATE, rect(32, 34, 46, 38)),
              (SLATE, rect(14, 43, 50, 46))]
    return layers


def processor():
    """A chip: dark body, silver legs on four sides, a die in the middle."""
    layers = []
    for i in range(4):
        y = 18 + i * 9
        layers.append((SILVER, rect(6, y, 14, y + 4)))
        layers.append((SILVER, rect(50, y, 58, y + 4)))
        x = 18 + i * 9
        layers.append((SILVER, rect(x, 6, x + 4, 14)))
        layers.append((SILVER, rect(x, 50, x + 4, 58)))
    layers.append((DARK, [(16, 14), (48, 14), (50, 16), (50, 48), (48, 50),
                          (16, 50), (14, 48), (14, 16)]))
    layers.append((BLUE, rect(24, 24, 40, 40)))
    layers.append((SILVER, rect(27, 27, 31, 31)))
    return layers


def memory():
    """A memory module: a green board, black chips, gold pads below."""
    layers = [(GREEN, rect(6, 14, 58, 46))]
    for i in range(4):
        layers.append((DARK, rect(10 + i * 12, 19, 19 + i * 12, 33)))
    for i in range(8):
        layers.append((AMBER, rect(9 + i * 6, 46, 13 + i * 6, 52)))
    layers.append((SILVER, rect(6, 42, 58, 44)))
    return layers


def storage():
    """A stack of three platters, the top one lit."""
    layers = []
    for i, tone in enumerate((SLATE, SLATE, DARK)):
        y = 14 + i * 14
        layers.append((tone, [(10, y), (54, y), (58, y + 4), (58, y + 10),
                              (54, y + 14), (10, y + 14), (6, y + 10),
                              (6, y + 4)]))
        layers.append((GREEN if i == 0 else SILVER, rect(48, y + 5, 53, y + 9)))
    layers.append((SILVER, [(12, 16), (52, 16), (55, 19), (12, 19)]))
    return layers


def power():
    """A lightning bolt in amber over a dark disc."""
    return [(DARK, circle(32, 32, 27, 32)),
            (AMBER, [(36, 8), (18, 36), (30, 36), (26, 56), (46, 26),
                     (34, 26)])]


def radio():
    """A mast with three arcs of signal leaving its tip."""
    layers = [(SLATE, [(30, 26), (34, 26), (40, 58), (24, 58)]),
              (DARK, circle(32, 24, 5, 16))]
    # three bows of signal above the tip, each wider than the last,
    # all short of the canvas corners
    for y, half in ((17, 9), (10, 15), (3, 21)):
        layers.append((BLUE, [(32 - half, y + 6), (32 + half, y + 6),
                              (32 + half - 3, y), (32 - half + 3, y)]))
    return layers


def security():
    """A padlock: a silver shackle over a carmine body with a keyhole."""
    return [(SILVER, ring(32, 24, 14, 9, 24)),
            (SLATE, rect(14, 24, 50, 30)),
            (CARMINE, [(12, 28), (52, 28), (54, 30), (54, 56), (52, 58),
                       (12, 58), (10, 56), (10, 30)]),
            (DARK, circle(32, 40, 5, 16)), (DARK, rect(30, 40, 34, 50))]


def boot():
    """The power mark: a ring with its bar, green as a board that came up."""
    return [(DARK, circle(32, 32, 27, 32)),
            (GREEN, ring(32, 34, 17, 12, 28)),
            (DARK, rect(26, 10, 38, 30)),
            (GREEN, rect(29, 12, 35, 34))]


ICONS = {"identity": identity, "processor": processor, "memory": memory,
         "storage": storage, "power": power, "radio": radio,
         "security": security, "boot": boot}
DESCRIPTIONS = {
    "identity": "the identity section's icon in Devices Preferences, a name "
                "tag, as HVIF bytes that tiku_icons_tiku.c includes.",
    "processor": "the processor section's icon in Devices Preferences, a "
                 "chip, as HVIF bytes that tiku_icons_tiku.c includes.",
    "memory": "the memory section's icon in Devices Preferences, a memory "
              "module, as HVIF bytes that tiku_icons_tiku.c includes.",
    "storage": "the storage section's icon in Devices Preferences, a stack of "
               "platters, as HVIF bytes that tiku_icons_tiku.c includes.",
    "power": "the power section's icon in Devices Preferences, an amber "
             "lightning bolt, as HVIF bytes that tiku_icons_tiku.c includes.",
    "radio": "the radio section's icon in Devices Preferences, a mast sending "
             "arcs of signal, which is also TikuSDR's icon, as HVIF bytes "
             "that tiku_icons_tiku.c includes.",
    "security": "the security section's icon in Devices Preferences, a "
                "padlock, as HVIF bytes that tiku_icons_tiku.c includes.",
    "boot": "the boot section's icon in Devices Preferences, a green power "
            "mark, as HVIF bytes that tiku_icons_tiku.c includes.",
}
name = sys.argv[1]
layers = ICONS[name]()
blob = icon([style_solid(c) for c, _ in layers],
            [path_polygon(p) for _, p in layers],
            [shape(i, i) for i in range(len(layers))])
print(header("tiku_icons_section_%s.inl" % name, DESCRIPTIONS[name],
             "tools/icons/mksectionicons.py %s" % name))
print("static const uint8_t tiku_icon_section_%s[] = {" % name)
for at in range(0, len(blob), 12):
    print("    " + ", ".join(f"0x{b:02x}" for b in blob[at:at+12]) + ",")
print("};")
