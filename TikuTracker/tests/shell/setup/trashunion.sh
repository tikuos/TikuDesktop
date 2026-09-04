# Set up before trashunion.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p /dev/shm/.Trash
echo hi > /dev/shm/.Trash/planted.txt
