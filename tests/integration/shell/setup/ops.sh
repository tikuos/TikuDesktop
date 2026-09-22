# Set up before ops.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

for gi in $(seq 1 400); do
               echo x > "$work/g$gi.txt"
           done
