# Set up before iderunboardlive.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same two names, in the same order, as every other editor fixture:
# the Open panel's rows are then where these scripts already know.
mkdir -p "$deskhome"
printf 'title Live\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
# Lights the lamp, then spends a minute of the board's time, and only
# THEN puts it out: while the lamp is lit the program is between its
# first line and its last, which is what makes a reading mid-run mean
# something.
printf '10 VFSWRITE "/dev/led0", 1\n20 FOR I = 1 TO 400000\n30 NEXT I\n40 VFSWRITE "/dev/led0", 0\n50 PRINT "DONE"\n' \
    > "$deskhome/blink.bas"
printf '10 VFSWRITE "/dev/led0", 0\n' > "$deskhome/quiet.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" "$appsdir/programming/"
