# Set up before crash.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# An application that fails on purpose, installed where the shell offers
# applications from.  It is built with the tests, not beside the real
# ones: nothing should be able to start it by accident.
mkdir -p "$appsdir"
cp -f "$top/build/tests/Fault.so" "$appsdir/"

# Reports go under the home, which this fixture makes fresh each run, so
# a name a script can write down is pointed at it.
mkdir -p "$deskhome/.config/tracker/reports"
rm -f "$tmp/trk_reports"
ln -s "$deskhome/.config/tracker/reports" "$tmp/trk_reports"
