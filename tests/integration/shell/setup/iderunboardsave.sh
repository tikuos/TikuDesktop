# Set up before iderunboardsave.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same names and order as the other editor fixtures, so the Open
# panel's rows are where every one of these scripts already knows.
mkdir -p "$deskhome"
printf 'title OnBoard\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
printf '10 PRINT "A"\n' > "$deskhome/blink.bas"
# The one kept: its name is the slot, and its text is what is read back.
printf '10 PRINT "KEPT"\n' > "$deskhome/quiet.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" "$appsdir/programming/"
