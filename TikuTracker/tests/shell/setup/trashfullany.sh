# Set up before trashfullany.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p /dev/shm/.Trash
# Also the union script's plant: it stays behind after a
# full run, and "empty at rest" must mean empty.
rm -f /dev/shm/.Trash/xplant.txt \
      /dev/shm/.Trash/planted.txt
