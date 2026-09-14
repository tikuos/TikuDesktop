# Set up before iderunboardstop.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same names and order as the other editor fixtures, so the Open
# panel's rows are where every one of these scripts already knows.
mkdir -p "$deskhome"
printf 'title OnBoard\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
# A minute of the board's time, at about a thousand loop steps a second:
# plainly still going when it is stopped.
printf '10 PRINT "GO"\n20 FOR I = 1 TO 60000\n30 NEXT I\n40 PRINT "END"\n' \
    > "$deskhome/blink.bas"
printf '10 PRINT "FROMBOARD"\n' > "$deskhome/quiet.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" "$appsdir/programming/"
