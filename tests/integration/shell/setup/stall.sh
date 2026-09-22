# Set up before stall.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# A legacy trusted installation must still be isolated. Its default
# auto-start policy survives migration; its former process privilege does not.
mkdir -p "$appsdir/trusted"
cp -f "$top/build/tests/Stall.so" "$appsdir/trusted/"

# Reports go under the home, which is made fresh each run, so a name a
# script can write down is pointed at it.
mkdir -p "$deskhome/.config/tracker/reports"
rm -f "$tmp/trk_reports"
ln -s "$deskhome/.config/tracker/reports" "$tmp/trk_reports"
