# Set up before jumps.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A BASIC program whose first line jumps to its last, which starts with
# blanks so the caret's pixel at the line's start is clear of its digits.
# Nothing else in the home, so the panel's first row is the file.
mkdir -p "$deskhome"
printf '10 GOTO 40\n20 PRINT 1\n30 PRINT 2\n  40 END\n' > "$deskhome/jumps.bas"
