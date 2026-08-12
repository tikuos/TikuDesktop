#!/bin/sh
# Run every shell script against the binary named in its first line.
#
# A script says which shell it is for with a "# shell: tracker|desktop|both"
# line.  "both" runs it twice, because both front-ends are the product and a
# behaviour proven in one is not proven in the other.
set -u
here=$(dirname "$0")
root=$here/../..
work=/tmp/trk_script
fails=0

for s in "$here"/*.script; do
    which=$(sed -n 's/^# shell: //p' "$s" | head -1)
    [ -z "$which" ] && which=both
    for bin in tracker desktop; do
        case $which in
            both) ;;
            "$bin") ;;
            *) continue ;;
        esac
        # Every script starts from a clean desktop: window frames and the
        # session persist now, so a script that inherited the last one's
        # would be clicking wherever that left its windows.  The session
        # PAIR is the exception -- inheriting is what it tests.
        case $(basename "$s") in
            session2.script) ;;
            *) rm -f "$HOME/.local/share/tracker/devices/local.state" ;;
        esac
        # The fixture every script starts from.  Rows sort folders first,
        # so row 0 is "sub", row 1 "a.txt", row 2 "b.txt" -- which is what
        # the click coordinates in the scripts are aimed at.
        rm -rf $work && mkdir -p $work/sub
        echo hi > $work/a.txt
        echo hi > $work/b.txt
        echo hi > $work/c.txt
        # A scrolling test needs more rows than a window holds.
        case $(basename "$s") in
            scroll.script) i=1; while [ $i -le 40 ]; do
                               echo hi > "$work/f$i.txt"; i=$((i+1)); done ;;
            dbdrop.script) rm -rf "$HOME/.config/deskbar" ;;
            # A name that exists nowhere else on the machine, so a search
            # for it has exactly one answer and an emptied panel -- which
            # matches everything -- cannot be mistaken for a restored one.
            query.script) touch "$work/zqxjv.txt"
                          rm -f "$HOME/.local/share/tracker/devices/local.state" ;;
            alert.script)  echo other > "$work/sub/a.txt" ;;
            thumb.script)  python3 - "$work" <<'PYEOF'
import struct,sys
w,h=64,48
rows=b"".join(bytes([(x*4)%256,(y*5)%256,200] ) for y in range(h)
              for x in range(w))
rows=b"".join((b"".join(bytes([(x*4)%256,(y*5)%256,200]) for x in range(w))
               + b"\0"*((4-(w*3)%4)%4)) for y in range(h))
d=struct.pack("<2sIHHI",b"BM",54+len(rows),0,0,54)+ \
  struct.pack("<IiiHHIIiiII",40,w,-h,1,24,0,len(rows),2835,2835,0,0)+rows
open(sys.argv[1]+"/picture.bmp","wb").write(d)
PYEOF
                           ;;
        esac
        printf '\n== %s (%s)\n' "$(basename "$s")" "$bin"
        # A script that says "# argv: none" is started with no path at all,
        # which is the only way to exercise what the shell opens on its own.
        if grep -q '^# argv: none' "$s"; then
            DISPLAY= "$root/build/$bin" -script "$s" || fails=$((fails+1))
        else
            DISPLAY= "$root/build/$bin" $work -script "$s" || fails=$((fails+1))
        fi
    done
done
rm -rf $work
if [ $fails -gt 0 ]; then
    printf '\n%d shell script(s) FAILED\n' $fails
    exit 1
fi
echo
echo "shell scripts hold in both front-ends"
