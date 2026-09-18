#!/bin/sh
# What the Finder starts when the bundle is opened.
#
# Two things the binary cannot work out for itself from inside a bundle.
#
# Where its applications are: the desktop finds them by climbing from
# its own path looking for an "apps" directory, which is right for a
# build tree and wrong for a bundle, where Resources is a sibling of
# MacOS rather than a parent of anything.  So it is told.
#
# And whether there is anywhere to draw at all.  A program started from
# the Finder inherits almost no environment -- no DISPLAY -- and a
# desktop with nowhere to draw exits at once.  From a terminal that
# prints a line; from a double-click it is a bounce in the Dock and
# nothing else, which tells a person nothing.  So the case is caught
# here and said in the one place a double-click can be answered.

here=$(cd "$(dirname "$0")" && pwd)
TIKU_APPS="$here/../Resources/apps"
export TIKU_APPS

# A desktop built on the Mac's own window (mk/toolchain.mk, TIKU_HOST)
# draws without any of the below: the bundle says which it carries.
if [ "$(cat "$here/../Resources/host" 2>/dev/null)" = cocoa ]; then
    if [ -n "${TIKU_LAUNCH_DRYRUN:-}" ]; then
        echo "cocoa: no X server needed"
        exit 0
    fi
    exec "$here/TikuDesktop-bin" "$@"
fi

if [ -z "${DISPLAY:-}" ]; then
    # An X server on a Mac announces itself as a launchd socket, and
    # there are two names for it: XQuartz's, and the one Homebrew's
    # xorg-server publishes through xinit.  launchd keeps them under
    # /var/run on current systems and under /private/tmp on older ones.
    # A socket FILE outlives the service that owned it -- launchd leaves
    # it on disk when the agent is unloaded -- and a client that connects
    # to one with nothing behind it hangs rather than failing.  So a
    # candidate counts only while its service is loaded in this user's
    # launchd domain.  (XQuartz normally exports DISPLAY session-wide,
    # so it seldom reaches this search at all.)
    uid=$(id -u)
    for candidate in $(ls -d /var/run/com.apple.launchd.*/org.xquartz:0 \
                              /var/run/com.apple.launchd.*/homebrew.mxcl:0 \
                              /private/tmp/com.apple.launchd.*/org.xquartz:0 \
                              /private/tmp/com.apple.launchd.*/homebrew.mxcl:0 \
                              2>/dev/null); do
        case "$candidate" in
            *org.xquartz:0)   service=org.xquartz.startx ;;
            *homebrew.mxcl:0) service=homebrew.mxcl.startx ;;
            *)                continue ;;
        esac
        launchctl print "gui/$uid/$service" > /dev/null 2>&1 || continue
        # Homebrew's startx hands the display over through a ROOT-owned
        # helper; with the user agent loaded and that helper absent the
        # server exits at once and a client hangs on the socket.  So the
        # helper must be there too, or this is not a server.
        if [ "$service" = homebrew.mxcl.startx ]; then
            launchctl print system/homebrew.mxcl.privileged_startx \
                > /dev/null 2>&1 || continue
        fi
        DISPLAY="$candidate"
        export DISPLAY
        break
    done
fi

if [ -n "${TIKU_LAUNCH_DRYRUN:-}" ]; then
    # For proving the search above without opening anything.
    printf '%s\n' "${DISPLAY:-<no display found>}"
    exit 0
fi

if [ -z "${DISPLAY:-}" ]; then
    osascript -e 'display alert "TikuDesktop needs an X server" message "This desktop draws through X11, and no X server is running.  Install XQuartz and open it once -- or, with Homebrew xorg-server and xinit, start the privileged startx service -- then open TikuDesktop again." as critical' > /dev/null 2>&1
    exit 1
fi

exec "$here/TikuDesktop-bin" "$@"
