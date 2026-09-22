# Set up before ideclip.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Two files, so a copy has somewhere to cross to; the
# desk's own Save panel is the third party.
mkdir -p "$deskhome"
printf 'title Clip\ngroup Sources\nfile main.bas\nfile util.bas\n' \
    > "$deskhome/project.tiku"
printf '10 alpha\n20 beta\n30 gamma\n' \
    > "$deskhome/main.bas"
printf '40 delta\n' > "$deskhome/util.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
