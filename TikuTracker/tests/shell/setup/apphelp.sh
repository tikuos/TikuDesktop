# Set up before apphelp.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The historical flag now starts the demo as an ordinary isolated app.
cp -R "$top/build/apps/framework-demo" "$appsdir/"
embed=1
