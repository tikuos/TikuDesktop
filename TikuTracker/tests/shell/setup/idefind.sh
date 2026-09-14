# Set up before idefind.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome"
printf 'title Find\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
printf '10 say 40 here\n20 rem\n30 say here\n40 done\n' \
    > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
