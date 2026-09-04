# Set up before alert.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

echo other > "$work/sub/a.txt"
