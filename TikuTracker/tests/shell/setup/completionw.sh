# Set up before completionw.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome/Documents"
echo q > "$deskhome/recipes.query"
