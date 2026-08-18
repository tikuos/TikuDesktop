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
    if [ -n "${TRK_SHELL_ONLY:-}" ] &&
       [ "$(basename "$s")" != "$TRK_SHELL_ONLY" ]; then
        continue
    fi
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
        #
        # EVERY script runs with HOME pointing at a fixture of its own.
        # The Desktop is the user's Desktop folder and a drag that misses
        # its window lands on it: run against the real home, a test drops
        # its files into the account and then reads a Desktop covered in
        # its own leftovers.
        deskhome="/tmp/trk_home"
        deskfill=""
        if grep -q '^# home: fixture' "$s"; then
            deskfill=yes
        fi
        case $(basename "$s") in
            session2.script) ;;
            # EVERY device file, not just the home one: the fixture lives
            # on /tmp, which is its own device with its own state file, and
            # leaving that one behind carried a previous script's view mode
            # into the next one -- which moves every coordinate it clicks.
            *) rm -f "$deskhome"/.local/share/tracker/devices/*.state
               # And the settings, for the same reason: a script that
               # turns a setting off leaves it off for every script after
               # it, including its own next run.
               rm -f "$deskhome/.config/TrackerSettings" ;;
        esac
        # A script that says "# work: home" gets its fixture under $HOME
        # instead of /tmp.  It matters for anything about the Trash: the
        # Trash resolves into $HOME whenever home shares the item's
        # filesystem, so a script asserting a path under $HOME/.Trash must
        # keep its files under $HOME -- a host where /tmp is its own
        # volume would otherwise resolve them somewhere else.
        work=/tmp/trk_script
        if grep -q '^# work: home' "$s"; then
            work="$deskhome/.trk-shell-fixture"
        fi
        # A Trash-window script uses an isolated HOME under /tmp.  It must
        # never point at the account's real Trash, which the fixture reset
        # below deliberately recreates from scratch.
        if grep -q '^# argv: trash' "$s"; then
            deskhome="$work/home"
            work="$deskhome/.Trash"
        fi
        # The fixture every script starts from.  Rows sort folders first,
        # so row 0 is "sub", row 1 "a.txt", row 2 "b.txt" -- which is what
        # the click coordinates in the scripts are aimed at.
        # The session PAIR is the exception again: what part one left in
        # the home IS what part two starts from, so wiping the home here
        # would leave part two with no session to reopen.
        case $(basename "$s") in
            session2.script) rm -rf /tmp/trk_script ;;
            *) rm -rf /tmp/trk_script "$deskhome" ;;
        esac
        mkdir -p $work/sub
        echo hi > $work/a.txt
        echo hi > $work/b.txt
        echo hi > $work/c.txt
        # A scrolling test needs more rows than a window holds.
        mkdir -p "$deskhome/Desktop"
        case $(basename "$s") in
            bandscroll.script|scroll.script|scrollbar.script|insertabove.script) i=1; while [ $i -le 40 ]; do
                               echo hi > "$work/f$i.txt"; i=$((i+1)); done ;;
            dbdrop.script) rm -rf "$deskhome/.config/deskbar" ;;
            dbprefs.script) rm -rf "$deskhome/.config/deskbar" ;;
            # A name that exists nowhere else on the machine, so a search
            # for it has exactly one answer and an emptied panel -- which
            # matches everything -- cannot be mistaken for a restored one.
            query.script) touch "$work/zqxjv.txt" ;;
            # A folder carrying a background picture, written the way the
            # store keeps one: a 64x48 bitmap of a known colour, and the
            # record naming it, tiled over the window.
            # A default desktop picture on disk and no record yet, so the
            # boot install has something to install; deskbg2 gets a record
            # of its own, which the install must leave alone.
            deskbg.script|deskbg2.script)
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
                if [ "$(basename "$s")" = deskbg2.script ]; then
                    setfattr -n 'user._trk/background' \
                        -v "0 600 400 $deskhome/.config/tracker/desktop-background" \
                        "$deskhome/Desktop"
                fi ;;
            background.script)
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
                         -v "3 0 0 $work/wall.bmp" "$work" ;;
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
        # A shared navigator preference is written by the multi-window
        # Preferences UI and observed by the browser process.  This fixture
        # starts with that external setting off so the observer is real.
        if grep -q '^# prefs: filter-on' "$s"; then
            mkdir -p "$deskhome/.config"
            printf 'TypeAheadFiltering on\n' > \
                "$deskhome/.config/TrackerSettings"
        fi
        if grep -q '^# prefs: navigator-off' "$s"; then
            prefhome="$deskhome"
            mkdir -p "$prefhome/.config"
            printf 'ShowNavigator off\n' > \
                "$prefhome/.config/TrackerSettings"
        fi
        printf '\n== %s (%s)\n' "$(basename "$s")" "$bin"
        # A script that says "# argv: none" is started with no path at all,
        # which is the only way to exercise what the shell opens on its own.
        # A script that says "# apps: fixture" gets an application registry
        # of its own: one application that writes down what it was handed,
        # which is the only way to check a launch really happened.
        if grep -q '^# apps: fixture' "$s"; then
            mkdir -p "$deskhome/.config/tracker"
            printf '#!/bin/sh\nprintf "%%s\\n" "$@" > /tmp/trk_launched\n' \
                > /tmp/trk_stamp.sh
            chmod +x /tmp/trk_stamp.sh
            rm -f /tmp/trk_launched
            printf 'Stamper\t/tmp/trk_stamp.sh\ttext/plain\n' \
                > "$deskhome/.config/tracker/apps"
        fi
        mkdir -p "$deskhome/Desktop"
        if [ -n "$deskfill" ]; then
            mkdir -p "$deskhome/Desktop/photos"
            echo hi > "$deskhome/Desktop/notes.txt"
            echo hi > "$deskhome/Desktop/todo.txt"
        fi
        if grep -q '^# argv: none' "$s"; then
            DISPLAY= HOME="$deskhome" "$root/build/$bin" \
                -script "$s" || fails=$((fails+1))
        else
            DISPLAY= HOME="$deskhome" "$root/build/$bin" $work \
                -script "$s" || fails=$((fails+1))
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
