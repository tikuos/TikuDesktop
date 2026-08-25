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
# ships (build/TikuDesktop, build/TikuTracker).  Those refuse -script loudly, so
# a harness pointed at the wrong one fails rather than passing quietly.
devbin="$root/build/dev"

# Scripts that are also run through the OUT-OF-PROCESS driver, against the
# shipping shell.  Keep this list to scripts written in the subset
# trk-conduct speaks (see tests/trk_conduct.c).
conducted="defaultbar.script narrate.script told.script mounts.script calert.script read.script board.script"

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
    HOME="$rsthome" "$root/build/TikuDesktop" -reset-settings > /dev/null
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
        # The header still names the LANE in one lower-case word, which is
        # what a script selects on; the executable is the product, and the
        # two stopped being the same string when it got its own name.
        case $bin in
            tracker) exe=TikuTracker ;;
            desktop) exe=TikuDesktop ;;
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
        splash=""
        if grep -q '^# home: fixture' "$s"; then
            deskfill=yes
        fi
        # The boot screen is off in a scripted run unless the script says
        # otherwise: its stages are held long enough to be READ, and every
        # other script here would pay that dwell to watch a screen it is
        # not looking at.
        if grep -q '^# splash:' "$s"; then
            splash=1
        fi
        # A second mount in the namespace.  There is no board on the host
        # that runs these, so the only way to put the routing under a real
        # window is to offer the local files again under another name --
        # which is the same shape, as far as everything above the backend
        # seam can tell.
        mountat=""
        if grep -q '^# mount: ' "$s"; then
            mountat=$(sed -n 's/^# mount: //p' "$s" | head -1)
        fi
        # Which path the shell is started on.  The engine has no verb for
        # opening one, so the window a script wants is the one it was
        # given on the command line -- and a script about the namespace
        # wants a window on a mounted name, not on the fixture.
        openat=""
        if grep -q '^# open: ' "$s"; then
            openat=$(sed -n 's/^# open: //p' "$s" | head -1)
        fi
        # EVERY script's desktop is a fixture's: the host's own volumes
        # must not land icons on it, or arrangement and repaint checks are
        # at the mercy of whatever this machine mounts and of when its
        # icons arrive (PVN-040's poses are proven by unit checks and
        # opted-in scripts instead).
        volbase=yes
        volumes_on=""
        if grep -q '^# prefs: volumes-on' "$s"; then
            volbase=""
            volumes_on=yes
        fi
        # A script may ask to run in another language, to prove the words
        # come from the catalogue rather than the code.
        lang=""
        langdir=""
        if grep -q '^# lang: ' "$s"; then
            lang=$(sed -n 's/^# lang: //p' "$s" | head -1)
            langdir="$root/strings"
        fi
        # A factory lane means EXACTLY that: nothing written, so what it
        # sees is what a person sees on a first run.
        if grep -q '^# prefs: factory' "$s"; then
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
        # What is INSTALLED is the fixture's to say: a lane must not
        # depend on whatever is sitting in the build tree, and a stray
        # trusted application would open a window in every script.
        appsdir=/tmp/trk_apps
        rm -rf "$appsdir"
        mkdir -p "$appsdir"
        cp -f "$root/../TikuDesktop/build/apps/terminal.so" \
              "$root/../TikuDesktop/build/apps/editor.so" "$appsdir/" \
            2>/dev/null || true

        mkdir -p $work/sub
        echo hi > $work/a.txt
        echo hi > $work/b.txt
        echo hi > $work/c.txt
        # A scrolling test needs more rows than a window holds.
        case $(basename "$s") in
            bandscroll.script|scroll.script|scrollbar.script|insertabove.script|resizefit.script|winbar.script|barfocus.script|stepperhold.script|stepperholdw.script|dimarrows.script|revealrename.script|revealrenamew.script) i=1; while [ $i -le 40 ]; do
                               echo hi > "$work/f$i.txt"; i=$((i+1)); done ;;
            dbdrop.script) rm -rf "$deskhome/.config/deskbar" ;;
            dbprefs.script) rm -rf "$deskhome/.config/deskbar" ;;
            # A name that exists nowhere else on the machine, so a search
            # for it has exactly one answer and an emptied panel -- which
            # matches everything -- cannot be mistaken for a restored one.
            query.script) touch "$work/zqxjv.txt" ;;
            # A program to double-click, and the mark it leaves when it
            # actually runs: the only evidence a launch really happened.
            runapp.script)
                rm -f /tmp/trk_ran
                printf '#!/bin/sh\ntouch /tmp/trk_ran\n' > "$work/runme"
                chmod +x "$work/runme" ;;
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
                mkdir -p "$deskhome/.config/tracker/Desktop"
                if [ "$(basename "$s")" = deskbg2.script ]; then
                    setfattr -n 'user._trk/background' \
                        -v "0 600 400 $deskhome/.config/tracker/desktop-background" \
                        "$deskhome/.config/tracker/Desktop"
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
            calert.script) echo other > "$work/sub/a.txt" ;;
            ops.script)    for gi in $(seq 1 400); do
                               echo x > "$work/g$gi.txt"
                           done ;;
            completionw.script)
                mkdir -p "$deskhome/Documents"
                echo q > "$deskhome/recipes.query" ;;
            completion.script)
                mkdir -p "$deskhome/Documents" ;;
            nameclash.script)
                mkdir -p "$deskhome/.config/tracker/Desktop/sub" ;;
            embedapp.script)
                # The SAME descriptor, hosted linked-in this time.
                embed=1 ;;
            deskprefs.script)
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
                ;;
            dropfont.script)
                # A face dropped where faces are dropped.  DejaVu is the
                # file the baked tables are made from, so a tree that can
                # build the fonts has this one to drop.
                mkdir -p "$deskhome/.config/tracker/fonts"
                for f in /usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf \
                         /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf; do
                    if [ -f "$f" ]; then
                        cp "$f" "$deskhome/.config/tracker/fonts/dropped.ttf"
                        break
                    fi
                done ;;
            icondir.script)
                # Art dropped where the desktop looks: the folder icon is
                # the printer's, which nothing would draw by accident.
                mkdir -p "$deskhome/.config/tracker/icons"
                "$root/build/icon-export" printer \
                    "$deskhome/.config/tracker/icons/folder.hvif" ;;
            appsdir.script)
                # Installed, not linked: a trusted copy for the desktop
                # to load, and a plain one for the runner to start.
                mkdir -p "$appsdir/trusted"
                cp -f "$root/../TikuDesktop/build/apps/sketch.so" "$appsdir/"
                cp -f "$root/../TikuDesktop/build/apps/clock.so" \
                      "$appsdir/trusted/" ;;
            terminal.script)
                # A program that paints one row at an ABSOLUTE position:
                # written to a file because typing backslashes through
                # the driver one key at a time proves nothing extra.
                printf '#!/bin/sh\nprintf "\\033[12;1H\\033[7m%%80s\\033[0m" ""\n' \
                    > /tmp/trk_term_cup.sh
                chmod +x /tmp/trk_term_cup.sh
                ( HOME="$deskhome" "$root/../TikuDesktop/build/tiku-term" \
                      >/dev/null 2>&1 & ) ;;
            remoteapp.script)
                # The out-of-process client: started BESIDE the desktop,
                # its connect retry covers the launch order.
                ( HOME="$deskhome" "$root/../TikuDesktop/build/tiku-demo-app" \
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
        # Asked for by name, and written AFTER the home is remade: volumes
        # on the Desktop is not what a device does by default any more, so
        # a lane about them says so instead of relying on a fresh install.
        if [ -n "$volumes_on" ]; then
            mkdir -p "$deskhome/.config"
            # Both, because the two are one question -- what the
            # Desktop shows for storage -- and this lane turns the
            # volumes off to watch the answer change.
            printf 'MountVolumesOntoDesktop on\nShowDisksIcon off\n' > \
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
        # Tracker's own Desktop, which is not the login session's.
        deskdir="$deskhome/.config/tracker/Desktop"
        mkdir -p "$deskdir"
        if [ -n "$deskfill" ]; then
            mkdir -p "$deskdir/photos"
            echo hi > "$deskdir/notes.txt"
            echo hi > "$deskdir/todo.txt"
        fi
        if grep -q '^# argv: none' "$s"; then
            DISPLAY= HOME="$deskhome" \
                env ${embed:+TIKU_EMBED_DEMO=1} ${splash:+TIKU_SPLASH=1} \
                ${mountat:+TIKU_MOUNT=$mountat} \
                ${lang:+TIKU_LANG=$lang} ${langdir:+TIKU_STRINGS=$langdir} \
                TIKU_APPS="$appsdir" "$devbin/$exe" \
                -script "$s" || fails=$((fails+1))
        else
            DISPLAY= HOME="$deskhome" \
                env ${embed:+TIKU_EMBED_DEMO=1} ${splash:+TIKU_SPLASH=1} \
                ${mountat:+TIKU_MOUNT=$mountat} \
                ${lang:+TIKU_LANG=$lang} ${langdir:+TIKU_STRINGS=$langdir} \
                TIKU_APPS="$appsdir" "$devbin/$exe" ${openat:-$work} \
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
    echo other > "$work/sub/a.txt"
    # Named on purpose (stage 3): the socket lane requires a token, so
    # every conducted script also proves the named-principal path; the
    # tty lane below stays tokenless and proves the other one.
    cmount=$(sed -n 's/^# mount: //p' "$cs" | head -1)
    copen=$(sed -n 's/^# open: //p' "$cs" | head -1)
    # Through env(1): a word an expansion produced is not an assignment
    # to the shell, and a lane that silently ran nothing would read as a
    # driver failure on a healthy driver.
    ( DISPLAY= HOME="$chome" TIKU_APPS=/tmp/trk_apps \
        env ${cmount:+TIKU_MOUNT=$cmount} \
        "$root/build/TikuDesktop" "${copen:-$work}" -conduct "$csock" \
        -conduct-token trk-drive > /dev/null 2>&1 & )
    "$root/build/dev/trk-conduct" -s "$csock" -n conducted -k trk-drive "$cs" ||
        fails=$((fails+1))
    # By name, not by pattern: a -f pattern matches the shell running
    # this script too, whose command line contains it.  The name is the
    # BINARY's -- "desktop" here killed nothing, and every conducted
    # script left its shell running for the rest of the suite.
    pkill -x TikuDesktop > /dev/null 2>&1
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
    echo other > "$work/sub/a.txt"
    DISPLAY= HOME="$chome" TIKU_APPS=/tmp/trk_apps \
        env ${cmount:+TIKU_MOUNT=$cmount} \
        "$root/build/dev/trk-conduct" \
        -n "over a line" "$cs" -- "$root/build/TikuDesktop" \
        "${copen:-$work}" ||
        fails=$((fails+1))
    pkill -x TikuDesktop > /dev/null 2>&1
    rm -rf "$chome"
done

rm -rf $work
if [ $fails -gt 0 ]; then
    printf '\n%d shell script(s) FAILED\n' $fails
    exit 1
fi
echo
echo "shell scripts hold in both front-ends"
