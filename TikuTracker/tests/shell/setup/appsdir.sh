# Set up before appsdir.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Installed, not linked: a trusted copy for the desktop
# to load, and a plain one for the runner to start.
mkdir -p "$appsdir/trusted"
cp -f "$top/build/apps/sketch.so" "$appsdir/"
cp -f "$top/build/apps/clock.so" \
      "$appsdir/trusted/"
