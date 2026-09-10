import sys; sys.path.insert(0, "/tmp")
from mkhvif import *

# ---- microcontroller: dark chip body, silver legs, pin-1 dot ----
LEG   = (0xB4, 0xB8, 0xBC, 0xFF)
BODY  = (0x33, 0x35, 0x3A, 0xFF)
TOP   = (0x4A, 0x4D, 0x54, 0xFF)
DOT   = (0xE0, 0xE2, 0xE4, 0xFF)
styles, paths, shapes = [], [], []
def add(st, pts):
    styles.append(style_solid(st)); paths.append(path_polygon(pts))
    shapes.append(shape(len(styles) - 1, len(paths) - 1))
# legs: four per side, drawn under the body
for i in range(4):
    y = 20 + i * 8
    add(LEG, rect(8, y, 18, y + 4))       # left
    add(LEG, rect(46, y, 56, y + 4))      # right
add(BODY, rect(16, 14, 48, 50))           # body
add(TOP,  rect(19, 17, 45, 30))           # lit top face
add(DOT,  circle(23, 44, 3.2))            # pin-1 dot
write("microcontroller", icon(styles, paths, shapes))

# ---- computer: monitor with a BeOS-blue screen on a stand ----
BEZEL = (0xD6, 0xD2, 0xC8, 0xFF)
DARK  = (0x9A, 0x96, 0x8E, 0xFF)
SCRN  = (0x36, 0x6A, 0xA0, 0xFF)
GLINT = (0x5E, 0x93, 0xC6, 0xFF)
styles, paths, shapes = [], [], []
def add2(st, pts):
    styles.append(style_solid(st)); paths.append(path_polygon(pts))
    shapes.append(shape(len(styles) - 1, len(paths) - 1))
add2(DARK,  rect(22, 46, 42, 54))         # stand neck
add2(DARK,  rect(14, 52, 50, 57))         # base
add2(BEZEL, rect(8, 10, 56, 47))          # bezel
add2(SCRN,  rect(12, 14, 52, 41))         # screen
add2(GLINT, [(14, 39), (26, 16), (32, 16), (20, 39)])   # glint
write("computer", icon(styles, paths, shapes))

# ---- magnifier: steel ring, pale glass, dark handle ----
GLASS = (0xBF, 0xD8, 0xEC, 0xFF)
RIM   = (0x6E, 0x76, 0x80, 0xFF)
GRIP  = (0x4A, 0x4030, 0x28, 0xFF)
styles, paths, shapes = [], [], []
def add3(st, pts):
    styles.append(style_solid(st)); paths.append(path_polygon(pts))
    shapes.append(shape(len(styles) - 1, len(paths) - 1))
HANDLE = (0x5A, 0x4A, 0x36, 0xFF)
add3(HANDLE, [(36, 38), (44, 30), (56, 46), (49, 53)])
add3(GLASS, circle(26, 26, 16))
add3(RIM,   ring(26, 26, 17, 14))
write("magnifier", icon(styles, paths, shapes))
