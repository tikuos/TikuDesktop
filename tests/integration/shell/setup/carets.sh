# Set up before carets.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Three short lines and a final line break.  Nothing else in the home, so
# the panel's first row is the file the script opens.
mkdir -p "$deskhome"
printf 'one\ntwo\nthree\n' > "$deskhome/carets.txt"
