#!/bin/sh
# A second volume, for the tests that are about having more than one.
#
# The Trash is a union across volumes, unmounting is refused or allowed
# per volume, and a copy that crosses one takes a different path through
# the backend than a copy that does not.  None of it can be shown on a
# host with a single disk, so the suite asks here for another one.
#
# Linux has /dev/shm, which is a volume of its own on every one of them
# and is what these tests have always used.  macOS has no such thing, so
# one is made: a ram disk, erased and mounted, which wants no privilege
# and lasts as long as the run that asked for it.
#
#   second-volume.sh open         prints the mount point, or nothing
#   second-volume.sh close PATH   gives it back
#
# An empty answer is not an error here.  The caller says what it could
# not prove, in the words of the test that wanted it -- a suite that
# skipped out loud reads as a skip, and one that invented a volume of
# its own under /tmp would read as a pass.

set -u

# The name is the handle: a run that was killed leaves the volume behind,
# and the next one has to recognise its own rather than mount a second.
name=trk-second-volume

case "${1:-}" in
open)
    # The host's own, where there is one.  Nothing is created and
    # nothing is given back, which is why close leaves it alone.
    if [ -d /dev/shm ] && [ -w /dev/shm ]; then
        printf '%s\n' /dev/shm
        exit 0
    fi
    command -v hdiutil > /dev/null 2>&1 || exit 0
    command -v diskutil > /dev/null 2>&1 || exit 0
    # Whatever a killed run left mounted under the name, before making
    # another one that would mount beside it as "trk-second-volume 1".
    [ -d "/Volumes/$name" ] && diskutil eject "/Volumes/$name" > /dev/null 2>&1
    # 32 MB in 512-byte sectors: more than every fixture put together,
    # and small enough that the erase is quicker than a test.
    dev=$(hdiutil attach -nomount ram://65536 2>/dev/null | tr -d ' \t')
    [ -n "$dev" ] || exit 0
    if diskutil eraseVolume APFS "$name" "$dev" > /dev/null 2>&1; then
        printf '%s\n' "/Volumes/$name"
        exit 0
    fi
    hdiutil detach "$dev" > /dev/null 2>&1
    ;;
close)
    # Only what this script made.  /dev/shm belongs to the host and
    # ejecting it would take the machine's own with it.
    case "${2:-}" in
        "/Volumes/$name") diskutil eject "/Volumes/$name" > /dev/null 2>&1 ;;
        *) ;;
    esac
    ;;
esac
exit 0
