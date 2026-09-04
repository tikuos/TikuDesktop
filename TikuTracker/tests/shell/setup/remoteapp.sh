# Set up before remoteapp.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The out-of-process client: started BESIDE the desktop,
# its connect retry covers the launch order.
( HOME="$deskhome" "$top/build/tiku-demo-app" \
      >/dev/null 2>&1 & )
