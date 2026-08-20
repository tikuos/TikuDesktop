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

# The scripted shells are the DEVELOPMENT flavour: the interpreter that
# reads these files is test infrastructure and is not linked into what
# ships (build/desktop, build/tracker).  Those refuse -script loudly, so
# a harness pointed at the wrong one fails rather than passing quietly.
devbin="$root/build/dev"

# Scripts that are also run through the OUT-OF-PROCESS driver, against the
# shipping shell.  Keep this list to scripts written in the subset
# trk-conduct speaks (see tests/trk_conduct.c).
conducted="defaultbar.script"

# The rescue path first: -reset-settings must move the settings and every
# device state aside WITHOUT drawing a pixel, for the day a setting makes
# the screen unreadable.
if [ -z "${TRK_SHELL_ONLY:-}" ] || [ "${TRK_SHELL_ONLY:-}" = "resetflag" ]; then
    rsthome=/tmp/trk_reset_home
    rm -rf "$rsthome"
    mkdir -p "$rsthome/.config" "$rsthome/.local/share/tracker/devices"
    echo "DarkMode on" > "$rsthome/.config/TrackerSettings"
    echo junk > "$rsthome/.local/share/tracker/devices/local.state"
    # The SHIPPING binary here on purpose: the rescue path is a product
    # feature, and this is the one check that proves what ships has it.
    HOME="$rsthome" "$root/build/desktop" -reset-settings > /dev/null
    if [ -e "$rsthome/.config/TrackerSettings" ] ||
       [ -e "$rsthome/.local/share/tracker/devices/local.state" ] ||
       [ ! -e "$rsthome/.config/TrackerSettings.factory-reset" ]; then
        echo "  [FAIL] -reset-settings leaves factory defaults"
        fails=$((fails+1))
    fi
    rm -rf "$rsthome"
fi

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
        embed=""
        if grep -q '^# home: fixture' "$s"; then
            deskfill=yes
        fi
        # EVERY script's desktop is a fixture's: the host's own volumes
        # must not land icons on it, or arrangement and repaint checks are
        # at the mercy of whatever this machine mounts and of when its
        # icons arrive (PVN-040's poses are proven by unit checks and
        # opted-in scripts instead).
        volbase=yes
        if grep -q '^# prefs: volumes-on' "$s"; then
            volbase=""
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
            bandscroll.script|scroll.script|scrollbar.script|insertabove.script|resizefit.script|winbar.script|barfocus.script|stepperhold.script|stepperholdw.script|dimarrows.script|revealrename.script|revealrenamew.script) i=1; while [ $i -le 40 ]; do
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
            completionw.script)
                echo q > "$deskhome/recipes.query" ;;
            nameclash.script)
                mkdir -p "$deskhome/Desktop/sub" ;;
            embedapp.script)
                # The SAME descriptor, hosted linked-in this time.
                embed=1 ;;
            terminal.script)
                # A program that paints one row at an ABSOLUTE position:
                # written to a file because typing backslashes through
                # the driver one key at a time proves nothing extra.
                printf '#!/bin/sh\nprintf "\\033[12;1H\\033[7m%%80s\\033[0m" ""\n' \
                    > /tmp/trk_term_cup.sh
                chmod +x /tmp/trk_term_cup.sh
                ( HOME="$deskhome" "$root/../desktop/build/tiku-term" \
                      >/dev/null 2>&1 & ) ;;
            remoteapp.script)
                # The out-of-process client: started BESIDE the desktop,
                # its connect retry covers the launch order.
                ( HOME="$deskhome" "$root/../desktop/build/tiku-about-app" \
                      >/dev/null 2>&1 & ) ;;
            safedefault.script)
                mkdir -p "$deskhome/.Trash" "$deskhome/.config/tracker"
                echo hi > "$deskhome/.Trash/planted.txt" ;;
            linkdrop.script)
                ln -s "$work/sub" "$work/alias"
                ln -s /nowhere-at-all "$work/broken" ;;
            trashunion.script)
                mkdir -p /dev/shm/.Trash
                echo hi > /dev/shm/.Trash/planted.txt ;;
            trashfullany.script)
                mkdir -p /dev/shm/.Trash
                # Also the union script's plant: it stays behind after a
                # full run, and "empty at rest" must mean empty.
                rm -f /dev/shm/.Trash/xplant.txt \
                      /dev/shm/.Trash/planted.txt ;;
            hoverpop.script)
                mkdir -p "$work/sub/quite-a-long-directory-name-that-will-not-fit-in-the-row"
                echo hi > "$work/sub/quite-a-long-directory-name-that-will-not-fit-in-the-row/deep.txt" ;;
            # A folder with an inner folder and a file, so the spring
            # menu has a level to descend into and a row to grey.
            springnav.script)
                mkdir -p "$work/sub/inner"
                echo hi > "$work/sub/doc.txt" ;;
            # An application that lives IN the folder, so a pose exists to
            # drop documents onto; the registry names it as the Dropper.
            appdrop.script)
                printf '#!/bin/sh\nprintf "%%s\\n" "$@" > /tmp/trk_launched\n' \
                    > "$work/zapp.sh"
                chmod +x "$work/zapp.sh"
                rm -f /tmp/trk_launched
                mkdir -p "$deskhome/.config/tracker"
                printf 'Dropper\t/tmp/trk_script/zapp.sh\ttext/plain\n' \
                    > "$deskhome/.config/tracker/apps" ;;
            brokenlink.script) ln -s /nowhere-at-all "$work/dangling" ;;
            thumboff.script)
                python3 - "$work" <<'PYOFF'
import struct,sys
w,h=64,48
rows=b"".join((b"".join(bytes([(x*4)%256,(y*5)%256,200]) for x in range(w))
               + b"\0"*((4-(w*3)%4)%4)) for y in range(h))
d=struct.pack("<2sIHHI",b"BM",54+len(rows),0,0,54)+ \
  struct.pack("<IiiHHIIiiII",40,w,-h,1,24,0,len(rows),2835,2835,0,0)+rows
open(sys.argv[1]+"/picture.bmp","wb").write(d)
PYOFF
                           ;;
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
        if [ -n "$volbase" ]; then
            mkdir -p "$deskhome/.config"
            printf 'MountVolumesOntoDesktop off\nShowDisksIcon on\nShowNavigator on\n' > \
                "$deskhome/.config/TrackerSettings"
        fi
        if grep -q '^# addons: fixture' "$s"; then
            mkdir -p "$deskhome/.config/tracker/add-ons"
            printf '#!/bin/sh\nprintf "%%s\\n" "$@" >> /tmp/trk_addon_ran\n' \
                > "$deskhome/.config/tracker/add-ons/stamp"
            chmod +x "$deskhome/.config/tracker/add-ons/stamp"
            rm -f /tmp/trk_addon_ran
        fi
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
        if grep -q '^# prefs: thumbs-off' "$s"; then
            mkdir -p "$deskhome/.config"
            printf 'GenerateImageThumbnails off\n' >> \
                "$deskhome/.config/TrackerSettings"
        fi
        if grep -q '^# prefs: fullpath-on' "$s"; then
            mkdir -p "$deskhome/.config"
            printf 'ShowFullPathInTitleBar on\n' > \
                "$deskhome/.config/TrackerSettings"
        fi
        # The global bar now defaults ON for people; the suite's scripts
        # are pinned to the classic layout, so the fixture pins the bar
        # off the way it freezes the clock.  A script that wants the bar
        # turns it on itself at runtime.
        if ! grep -q '^# prefs: factory' "$s"; then
            mkdir -p "$deskhome/.config"
            printf 'GlobalMenuBar off\n' >> "$deskhome/.config/TrackerSettings"
        fi
        printf '\n== %s (%s)\n' "$(basename "$s")" "$bin"
        # A script that says "# argv: none" is started with no path at all,
        # which is the only way to exercise what the shell opens on its own.
        # A script that says "# apps: fixture" gets an application registry
        # of its own: one application that writes down what it was handed,
        # which is the only way to check a launch really happened.
        if grep -q '^# apps: fixture3' "$s"; then
            mkdir -p "$deskhome/.config/tracker"
            printf '#!/bin/sh\nprintf "%%s\\n" "$@" > /tmp/trk_launched\n' \
                > /tmp/trk_stamp.sh
            chmod +x /tmp/trk_stamp.sh
            rm -f /tmp/trk_launched
            printf 'Stamper\t/tmp/trk_stamp.sh\ttext/plain\nTexty\t/tmp/trk_stamp.sh\ttext/\nAnything\t/tmp/trk_stamp.sh\t*\n' \
                > "$deskhome/.config/tracker/apps"
        elif grep -q '^# apps: fixture' "$s"; then
            mkdir -p "$deskhome/.config/tracker"
            printf '#!/bin/sh\nprintf "%%s\\n" "$@" > /tmp/trk_launched\n' \
                > /tmp/trk_stamp.sh
            chmod +x /tmp/trk_stamp.sh
            rm -f /tmp/trk_launched
            printf 'Stamper\t/tmp/trk_stamp.sh\ttext/plain\nOddball\t/tmp/trk_stamp.sh\timage/png\n' \
                > "$deskhome/.config/tracker/apps"
        fi
        mkdir -p "$deskhome/Desktop"
        if [ -n "$deskfill" ]; then
            mkdir -p "$deskhome/Desktop/photos"
            echo hi > "$deskhome/Desktop/notes.txt"
            echo hi > "$deskhome/Desktop/todo.txt"
        fi
        if grep -q '^# argv: none' "$s"; then
            DISPLAY= HOME="$deskhome" \
                env ${embed:+TIKU_EMBED_ABOUT=1} "$devbin/$bin" \
                -script "$s" || fails=$((fails+1))
        else
            DISPLAY= HOME="$deskhome" \
                env ${embed:+TIKU_EMBED_ABOUT=1} "$devbin/$bin" $work \
                -script "$s" || fails=$((fails+1))
        fi
    done
done
# The conducted lane: the SHIPPING desktop, driven from OUTSIDE its
# process by trk-conduct over the conductor channel.  Every script above
# was read by an interpreter compiled into the shell; these are read by a
# program that was never linked against Tracker at all, and the shell it
# drives is the one that goes on a device.  A script listed here must be
# written in the subset both drivers speak, so the same file proves the
# same thing twice, two ways.
for s in $conducted; do
    cs="$root/tests/shell/$s"
    [ -e "$cs" ] || continue
    printf '\n== %s (conducted, shipping shell)\n' "$s"
    chome=/tmp/trk_conduct_home
    csock=/tmp/trk_conduct.sock
    rm -rf "$chome" "$work" "$csock"
    mkdir -p "$chome/Desktop" "$work/sub"
    echo hi > "$work/a.txt"
    echo hi > "$work/b.txt"
    echo hi > "$work/c.txt"
    ( DISPLAY= HOME="$chome" "$root/build/desktop" "$work" \
        -conduct "$csock" > /dev/null 2>&1 & )
    "$root/build/dev/trk-conduct" -s "$csock" -n conducted "$cs" ||
        fails=$((fails+1))
    # By name, not by pattern: a -f pattern matches the shell running
    # this script too, whose command line contains it.
    pkill -x desktop > /dev/null 2>&1
    rm -rf "$chome" "$csock"

    # And again over a LINE: the driver makes a pty pair, starts the same
    # shipping shell on the far end with -conduct-tty, and speaks the
    # channel down it.  This is the rehearsal for a real board -- a raw
    # terminal with a line discipline and a baud rate, only the copper
    # missing -- and it is the reason the driver was put outside the
    # process in the first place.
    printf '\n== %s (conducted over a line, shipping shell)\n' "$s"
    rm -rf "$chome" "$work"
    mkdir -p "$chome/Desktop" "$work/sub"
    echo hi > "$work/a.txt"
    echo hi > "$work/b.txt"
    echo hi > "$work/c.txt"
    DISPLAY= HOME="$chome" "$root/build/dev/trk-conduct" \
        -n "over a line" "$cs" -- "$root/build/desktop" "$work" ||
        fails=$((fails+1))
    pkill -x desktop > /dev/null 2>&1
    rm -rf "$chome"
done

rm -rf $work
if [ $fails -gt 0 ]; then
    printf '\n%d shell script(s) FAILED\n' $fails
    exit 1
fi
echo
echo "shell scripts hold in both front-ends"
