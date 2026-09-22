# Set up before ideundo.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome"
printf 'title Undo\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
printf '10 alpha\n20 beta\n30 gamma\n' \
    > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
