# Set up before idepath.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome"
printf 'title Path\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
printf '10 print "/dev/led"\n20 print "hello there"\n' \
    > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
