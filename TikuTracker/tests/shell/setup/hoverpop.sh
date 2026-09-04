# Set up before hoverpop.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$work/sub/quite-a-long-directory-name-that-will-not-fit-in-the-row"
echo hi > "$work/sub/quite-a-long-directory-name-that-will-not-fit-in-the-row/deep.txt"
