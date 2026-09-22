# Set up before ideedit.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A project whose one file is 40 numbered lines, so the
# caret has somewhere to travel and the page has more
# than a screenful to reveal.  Files beside the
# description, where the panel opens.
mkdir -p "$deskhome"
printf 'title Nav\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
: > "$deskhome/main.bas"
i=1
while [ $i -le 40 ]; do
    printf '%d line%02d\n' $((i*10)) $i \
        >> "$deskhome/main.bas"
    i=$((i+1))
done
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
