"""Author simple solid-colour HVIF icons (64x64 canvas)."""
import struct, os

def coord(v):
    if float(v).is_integer() and -32 <= v <= 95:
        return bytes([int(v) + 32])
    n = int(round((v + 128.0) * 102.0))
    n = max(0, min(0x7FFF, n))
    return bytes([0x80 | (n >> 8), n & 0xFF])

def style_solid(rgba):
    r, g, b, a = rgba
    return bytes([1, r, g, b, a])

def path_polygon(points, closed=True):
    flags = 0x08 | (0x02 if closed else 0)   # NO_CURVES | CLOSED
    out = bytes([flags, len(points)])
    for (x, y) in points:
        out += coord(x) + coord(y)
    return out

def shape(style_idx, path_idx):
    return bytes([10, style_idx, 1, path_idx, 0])

def icon(styles, paths, shapes):
    out = b"ncif"
    out += bytes([len(styles)]) + b"".join(styles)
    out += bytes([len(paths)])  + b"".join(paths)
    out += bytes([len(shapes)]) + b"".join(shapes)
    return out

def circle(cx, cy, r, n=24):
    import math
    return [(cx + r * math.cos(2 * math.pi * i / n),
             cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]

def ring(cx, cy, ro, ri, n=24):
    """Annulus as one closed path: outer CW then inner CCW."""
    o = circle(cx, cy, ro, n)
    i = circle(cx, cy, ri, n)[::-1]
    return o + [o[0]] + i + [i[0]]

def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]

def write(name, blob):
    with open("/tmp/newicons/%s.hvif" % name, "wb") as f:
        f.write(blob)
    print("%-16s %4d bytes" % (name, len(blob)))
