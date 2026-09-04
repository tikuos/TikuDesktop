# Set up before background.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

python3 - "$work" <<'PYBG'
import struct,sys
w,h=64,48
row=b"".join(bytes([32,64,192]) for _ in range(w)) + b"\0"*((4-(w*3)%4)%4)
rows=row*h
d=struct.pack("<2sIHHI",b"BM",54+len(rows),0,0,54)+ \
  struct.pack("<IiiHHIIiiII",40,w,-h,1,24,0,len(rows),2835,2835,0,0)+rows
open(sys.argv[1]+"/wall.bmp","wb").write(d)
PYBG
setfattr -n 'user._trk/background' \
         -v "3 0 0 $work/wall.bmp" "$work"
