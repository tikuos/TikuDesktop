# Set up before idenew.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# NOTHING is planted -- except the person's own splash
# banner, which is the one thing a cold start may wear:
# a picture put where the program looks is chosen.
mkdir -p "$deskhome/.config/TikuBasic"
python3 - "$deskhome" <<'PYSPLASH'
import struct,sys
w,h=64,48
row=b"".join(bytes([40,90,180]) for _ in range(w)) + b"\0"*((4-(w*3)%4)%4)
rows=row*h
d=struct.pack("<2sIHHI",b"BM",54+len(rows),0,0,54)+ \
  struct.pack("<IiiHHIIiiII",40,w,-h,1,24,0,len(rows),2835,2835,0,0)+rows
open(sys.argv[1]+"/.config/TikuBasic/splash.bmp","wb").write(d)
PYSPLASH
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
