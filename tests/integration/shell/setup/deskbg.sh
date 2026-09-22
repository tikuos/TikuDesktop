# Set up before deskbg.script/deskbg2.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s
# and its helpers: set_xattr.
# A folder carrying a background picture, written the way the
# store keeps one: a 64x48 bitmap of a known colour, and the
# record naming it, tiled over the window.
# A default desktop picture on disk and no record yet, so the
# boot install has something to install; deskbg2 gets a record
# of its own, which the install must leave alone.

mkdir -p "$deskhome/.config/tracker"
python3 - "$deskhome/.config/tracker" <<'PYDB'
import struct,sys
w,h=80,60
row=b"".join(bytes([16,200,16]) for _ in range(w)) + b"\0"*((4-(w*3)%4)%4)
rows=row*h
d=struct.pack("<2sIHHI",b"BM",54+len(rows),0,0,54)+ \
  struct.pack("<IiiHHIIiiII",40,w,-h,1,24,0,len(rows),2835,2835,0,0)+rows
open(sys.argv[1]+"/desktop-background","wb").write(d)
PYDB
mkdir -p "$deskhome/.config/tracker/Desktop"
if [ "$(basename "$s")" = deskbg2.script ]; then
    set_xattr 'user._trk/background' \
        "0 600 400 $deskhome/.config/tracker/desktop-background" \
        "$deskhome/.config/tracker/Desktop"
fi
