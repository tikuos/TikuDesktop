# Set up before deskprefs.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A picture to choose, on the Desktop where a person
# would drop one.
mkdir -p "$deskhome/.config/tracker/Desktop"
python3 - "$deskhome/.config/tracker/Desktop" <<'PYDP'
import struct,sys
# Green with a red corner, so SCALED and TILED do not look alike: a
# solid picture is laid every way at once as far as a pixel can tell.
w,h=40,30
pad=b"\0"*((4-(w*3)%4)%4)
rows=b""
for y in range(h):
    line=b""
    for x in range(w):
        line += bytes([16,200,16]) if (x>=8 or y>=6) else bytes([16,16,200])
    rows += line + pad
d=struct.pack("<2sIHHI",b"BM",54+len(rows),0,0,54)+ \
  struct.pack("<IiiHHIIiiII",40,w,-h,1,24,0,len(rows),2835,2835,0,0)+rows
open(sys.argv[1]+"/meadow.bmp","wb").write(d)
PYDP
