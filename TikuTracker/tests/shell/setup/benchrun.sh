# Set up before benchrun.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# The bench front-end is an application like any other; it is put where
# the shell looks for applications, beside the ones the other scripts use.
mkdir -p "$appsdir"
cp -f "$top/build/apps/Bench.so" "$appsdir/"

# The run's log goes where a person keeps theirs -- under the home --
# and this fixture's home is made fresh for every run, so a name a
# script can write down is pointed at it.
rm -f "$tmp/trk_bench_logs"
ln -s "$deskhome/bench" "$tmp/trk_bench_logs"
