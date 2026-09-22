# Set up before trashfullany.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $vol2sub.

mkdir -p "$vol2sub/.Trash"
# Also the union script's plant: it stays behind after a
# full run, and "empty at rest" must mean empty.
rm -f "$vol2sub/.Trash/xplant.txt" \
      "$vol2sub/.Trash/planted.txt"
