#!/bin/sh
# Run every shell script against the binary named in its first line.
#
# A script says which shell it is for with a "# shell: tracker|desktop|both"
# line.  "both" runs it twice, because both front-ends are the product and a
# behaviour proven in one is not proven in the other.
set -u
here=$(dirname "$0")
root=$here/../..
# Everything is built at the top of the applications tree, into one
# build/ -- the shells, the driver, the applications this suite starts.
top=$root/..
work=/tmp/trk_script
fails=0

# The scripted shells are the DEVELOPMENT flavour: the interpreter that
# reads these files is test infrastructure and is not linked into what
# ships (build/TikuDesktop, build/TikuTracker).  Those refuse -script loudly, so
# a harness pointed at the wrong one fails rather than passing quietly.
devbin="$top/build/dev"

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
    HOME="$rsthome" "$top/build/TikuDesktop" -reset-settings > /dev/null
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
    # A run that was cut short is finished from where it stopped: names
    # BEFORE this one (the alphabet already walked) are passed by.
    if [ -n "${TRK_SHELL_FROM:-}" ]; then
        first=$(printf '%s\n%s\n' "$(basename "$s")" "$TRK_SHELL_FROM" \
                | LC_ALL=C sort | head -1)
        if [ "$first" != "$TRK_SHELL_FROM" ]; then
            continue
        fi
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
        cp -f "$top/build/apps/terminal.so" \
              "$top/build/apps/editor.so" "$appsdir/" \
            2>/dev/null || true

        mkdir -p $work/sub
        echo hi > $work/a.txt
        echo hi > $work/b.txt
        echo hi > $work/c.txt
        # The simplest setup a script carries as headers of its own:
        # "# mkdir: ~/Documents", "# file: work/sub/a.txt other",
        # "# touch:", "# rm:", "# link: target path".  ~/ is the
        # script's home and work/ its fixture; anything else is as
        # written.  In the order the script says them.
        grep -E '^# (mkdir|file|touch|rm|link): ' "$s" | while IFS= read -r line; do
            kind=${line#\# }; kind=${kind%%:*}; args=${line#*: }
            set -- $args
            expand() { case $1 in "~/"*) printf '%s' "$deskhome/${1#"~/"}" ;; work/*) printf '%s' "$work/${1#work/}" ;; *) printf '%s' "$1" ;; esac; }
            case $kind in
                mkdir) for p in "$@"; do mkdir -p "$(expand "$p")"; done ;;
                file)  p=$(expand "$1"); shift; printf '%s\n' "$*" > "$p" ;;
                touch) : > "$(expand "$1")" ;;
                rm)    rm -rf "$(expand "$1")" ;;
                link)  ln -s "$(expand "$1")" "$(expand "$2")" ;;
            esac
        done
        # What one script needs laid out before it starts lives beside
        # the scripts, in setup/<name>.sh: the file a "# setup:" header
        # names, else the one named after the script, else nothing.
        setup=$(sed -n 's/^# setup: //p' "$s" | head -1)
        [ -z "$setup" ] && setup=$(basename "$s" .script)
        if [ -f "$here/setup/$setup.sh" ]; then
            . "$here/setup/$setup.sh"
        fi
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
                TIKU_APPS="$appsdir" \
                TIKU_BASIC="$(cd "$top" && pwd)/build/tiku-basic" \
                "$devbin/$exe" \
                -script "$s" || fails=$((fails+1))
        else
            DISPLAY= HOME="$deskhome" \
                env ${embed:+TIKU_EMBED_DEMO=1} ${splash:+TIKU_SPLASH=1} \
                ${mountat:+TIKU_MOUNT=$mountat} \
                ${lang:+TIKU_LANG=$lang} ${langdir:+TIKU_STRINGS=$langdir} \
                TIKU_APPS="$appsdir" \
                TIKU_BASIC="$(cd "$top" && pwd)/build/tiku-basic" \
                "$devbin/$exe" ${openat:-$work} \
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
        "$top/build/TikuDesktop" "${copen:-$work}" -conduct "$csock" \
        -conduct-token trk-drive > /dev/null 2>&1 & )
    "$top/build/dev/trk-conduct" -s "$csock" -n conducted -k trk-drive "$cs" ||
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
        "$top/build/dev/trk-conduct" \
        -n "over a line" "$cs" -- "$top/build/TikuDesktop" \
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
