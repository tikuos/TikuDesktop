# Set up before springnav.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.
# A folder with an inner folder and a file, so the spring
# menu has a level to descend into and a row to grey.

mkdir -p "$work/sub/inner"
echo hi > "$work/sub/doc.txt"
