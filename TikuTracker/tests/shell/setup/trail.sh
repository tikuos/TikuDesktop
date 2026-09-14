# Set up before iderunboard.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same names and the same order as the other editor fixtures, so the
# panel's rows are where every one of these scripts already knows.
mkdir -p "$deskhome"
printf 'title OnBoard\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
# What the board prints can only have come from running it there.
printf '10 PRINT "FROMBOARD"\n20 PRINT 6 * 7\n' > "$deskhome/quiet.bas"
# A mistake on a known line: the board's interpreter blames a line the
# same way this machine's does.
printf '10 PRINT "A"\n20 PRINT UNDEFINED$(\n' > "$deskhome/blink.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
