# Set up before stall.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# An application that stops the shell's turn, put where applications are
# loaded INTO this process: its tick runs on the shell's own loop, which
# is the only way one program can freeze another here.  Built with the
# tests, never installed beside the real ones.
mkdir -p "$appsdir/trusted"
cp -f "$top/build/tests/Stall.so" "$appsdir/trusted/"

# Reports go under the home, which is made fresh each run, so a name a
# script can write down is pointed at it.
mkdir -p "$deskhome/.config/tracker/reports"
rm -f "$tmp/trk_reports"
ln -s "$deskhome/.config/tracker/reports" "$tmp/trk_reports"
