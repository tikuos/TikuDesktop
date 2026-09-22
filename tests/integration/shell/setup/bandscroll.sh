# Set up before the 12 scripts naming it runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

i=1; while [ $i -le 40 ]; do
               echo hi > "$work/f$i.txt"; i=$((i+1)); done
