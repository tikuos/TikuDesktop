# Set up before idewheel.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A page longer than the window, so the wheel has
# somewhere to roll it.
mkdir -p "$deskhome"
printf 'title Wheel\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
: > "$deskhome/main.bas"
i=1
while [ $i -le 40 ]; do
    printf '%d line%02d\n' $((i*10)) $i \
        >> "$deskhome/main.bas"
    i=$((i+1))
done
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
