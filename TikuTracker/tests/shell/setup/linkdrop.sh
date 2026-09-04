# Set up before linkdrop.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

ln -s "$work/sub" "$work/alias"
ln -s /nowhere-at-all "$work/broken"
