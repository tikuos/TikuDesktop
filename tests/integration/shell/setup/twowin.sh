# Set up before twowin.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# Trusted, so the two windows are open before the script
# touches anything: what is being proven is the runtime
# keeping them apart, not the road that starts them.
mkdir -p "$appsdir/trusted"
cp -f "$top/build/runtime/apps/twowin/twowin.so" \
      "$appsdir/trusted/"
rm -rf "$tmp/trk_twowin"
