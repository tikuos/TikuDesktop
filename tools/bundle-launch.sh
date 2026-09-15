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

if [ -z "${DISPLAY:-}" ]; then
    # XQuartz publishes its socket per user; if it is running, this is
    # where it says so, and a bundle should use it rather than refuse.
    for candidate in $(ls -d /private/tmp/com.apple.launchd.*/org.xquartz:0 \
                              /tmp/com.apple.launchd.*/org.xquartz:0 \
                              2>/dev/null); do
        DISPLAY="$candidate"
        export DISPLAY
        break
    done
fi

if [ -z "${DISPLAY:-}" ]; then
    osascript -e 'display alert "TikuDesktop needs an X server" message "This desktop draws through X11, and nothing is running to draw on.  Install XQuartz and open it once, then open TikuDesktop again." as critical' > /dev/null 2>&1
    exit 1
fi

exec "$here/TikuDesktop-bin" "$@"
