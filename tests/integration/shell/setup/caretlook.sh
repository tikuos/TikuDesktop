# Set up before caretlook.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Three short lines and nothing else in the home, so the panel's first
# row is the file the script opens.
mkdir -p "$deskhome"
printf 'first line\nsecond line\nthird line\n' > "$deskhome/looks.txt"
