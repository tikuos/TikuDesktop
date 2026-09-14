# Set up before report.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# Reports go under the home, which is made fresh each run, so a name a
# script can write down is pointed at it.
mkdir -p "$deskhome/.config/tracker/reports"
rm -f "$tmp/trk_reports"
ln -s "$deskhome/.config/tracker/reports" "$tmp/trk_reports"
