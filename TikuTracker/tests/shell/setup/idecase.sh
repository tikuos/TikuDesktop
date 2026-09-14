# Set up before idecase.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome"
printf 'title Case\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
printf '10 \n' > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
