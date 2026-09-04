# Set up before idemenu.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Two files, an unknown line the description must keep,
# and a program that waits on INPUT so a run can be
# stopped from the menu.
mkdir -p "$deskhome"
printf 'title Menu\nboard zebra\ngroup Sources\nfile main.bas\nfile util.bas\n' \
    > "$deskhome/project.tiku"
printf '10 rem go\n20 input a$\n' > "$deskhome/main.bas"
printf '40 rem other\n' > "$deskhome/util.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
