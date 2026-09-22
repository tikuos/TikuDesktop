# Set up before idescroll.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same 40-line program, to give the page a tail
# longer than the window so the scrollbar has work.
mkdir -p "$deskhome"
printf 'title Scroll\ngroup Sources\nfile main.bas\n' \
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
