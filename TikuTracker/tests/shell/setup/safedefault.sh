# Set up before safedefault.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome/.Trash" "$deskhome/.config/tracker"
echo hi > "$deskhome/.Trash/planted.txt"
