# Set up before benchrun.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The bench front-end is an application like any other; it is put where
# the shell looks for applications, beside the ones the other scripts use.
mkdir -p "$appsdir"
cp -f "$top/build/runtime/apps/bench/Bench.so" "$appsdir/"
