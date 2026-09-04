# Set up before dropfont.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

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
done
