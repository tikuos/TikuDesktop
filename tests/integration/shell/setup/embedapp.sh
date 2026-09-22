# Set up before embedapp.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The legacy switch now launches the installed demo in its own process.
cp -R "$top/build/runtime/apps/framework-demo" "$appsdir/"
embed=1
