# Set up before dropfont.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, and $skip.

# A face dropped where faces are dropped.  DejaVu is the
# file the baked tables are made from, so a tree that can
# build the fonts has this one to drop -- and the script
# asserts the family name the file gives itself, so no
# other face will do.
mkdir -p "$deskhome/.config/tracker/fonts"
dropped=""
for f in /usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf \
         /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
         /usr/local/share/fonts/DejaVuSans.ttf \
         /opt/homebrew/share/fonts/DejaVuSans.ttf \
         "$HOME/Library/Fonts/DejaVuSans.ttf" \
         /Library/Fonts/DejaVuSans.ttf; do
    if [ -f "$f" ]; then
        cp "$f" "$deskhome/.config/tracker/fonts/dropped.ttf"
        dropped=$f
        break
    fi
done
# Said rather than left to fail: the script asks for "DejaVu Sans" by
# name, so without the file the three Down keys land on a baked family
# and the lane would read as the desktop getting the wrong answer.
if [ -z "$dropped" ]; then
    skip="no DejaVu Sans on this host to drop (the script asks for it by name)"
fi
