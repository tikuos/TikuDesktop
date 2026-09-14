# Set up before twowinremote.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# Installed, not trusted: the runner starts it in a
# process of its own, so the windows are held over the
# wire rather than in this one.
cp -f "$top/build/apps/twowin.so" \
      "$appsdir/"
rm -rf "$tmp/trk_twowin"
