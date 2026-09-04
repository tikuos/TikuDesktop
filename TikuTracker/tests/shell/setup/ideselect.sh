# Set up before ideselect.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Three short lines, so a selection is easy to name and
# read back after it is typed over.
mkdir -p "$deskhome"
printf 'title Select\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
printf '10 alpha\n20 beta\n30 gamma\n' \
    > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
