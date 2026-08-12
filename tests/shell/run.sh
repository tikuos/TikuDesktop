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
        # The fixture every script starts from.  Rows sort folders first,
        # so row 0 is "sub", row 1 "a.txt", row 2 "b.txt" -- which is what
        # the click coordinates in the scripts are aimed at.
        rm -rf $work && mkdir -p $work/sub
        echo hi > $work/a.txt
        echo hi > $work/b.txt
        echo hi > $work/c.txt
        printf '\n== %s (%s)\n' "$(basename "$s")" "$bin"
        DISPLAY= "$root/build/$bin" $work -script "$s" || fails=$((fails+1))
    done
done
rm -rf $work
if [ $fails -gt 0 ]; then
    printf '\n%d shell script(s) FAILED\n' $fails
    exit 1
fi
echo
echo "shell scripts hold in both front-ends"
