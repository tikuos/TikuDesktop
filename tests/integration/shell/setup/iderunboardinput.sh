# Set up before iderunboardinput.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same names and order as the other editor fixtures, so the Open
# panel's rows are where every one of these scripts already knows.
mkdir -p "$deskhome"
printf 'title OnBoard\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
# The program asks, and misbehaves on a KNOWN line only when the answer
# is the one typed here: the error naming that line is the proof that the
# answer reached the program, in a form the message window can be asked
# about.
printf '10 INPUT "NAME"; A$\n20 IF A$ = "tiku" THEN PRINT UNDEFINED$(\n30 PRINT "DONE"\n' \
    > "$deskhome/blink.bas"
printf '10 PRINT "FROMBOARD"\n' > "$deskhome/quiet.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" "$appsdir/programming/"
