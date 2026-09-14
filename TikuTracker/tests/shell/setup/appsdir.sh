# Set up before appsdir.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Installed, not linked: a legacy auto-start copy and a menu-launched app.
mkdir -p "$appsdir/trusted"
cp -f "$top/build/apps/sketch/sketch.so" "$appsdir/"
cp -f "$top/build/apps/clock/clock.so" \
      "$appsdir/trusted/"
