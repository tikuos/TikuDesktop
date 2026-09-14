# Set up before idetoolbar.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome"
printf 'title Bar\ngroup Sources\nfile main.bas\n' > "$deskhome/project.tiku"
printf '3 goto 9\n9 input a$\n' > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" "$appsdir/programming/"
