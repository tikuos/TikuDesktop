# Set up before iderenum.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Numbers out of tens and a GOTO whose target must follow
# when they are renumbered.
mkdir -p "$deskhome"
printf 'title Renum\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
printf '5 rem start\n7 goto 23\n23 print "hi"\n' \
    > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
