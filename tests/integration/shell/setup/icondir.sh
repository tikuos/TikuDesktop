# Set up before icondir.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Art dropped where the desktop looks: the folder icon is
# the printer's, which nothing would draw by accident.
mkdir -p "$deskhome/.config/tracker/icons"
"$top/build/tools/icon-export" printer \
    "$deskhome/.config/tracker/icons/folder.hvif"
