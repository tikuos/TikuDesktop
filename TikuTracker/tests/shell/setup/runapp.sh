# Set up before runapp.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.
# A program to double-click, and the mark it leaves when it
# actually runs: the only evidence a launch really happened.

rm -f /tmp/trk_ran
printf '#!/bin/sh\ntouch /tmp/trk_ran\n' > "$work/runme"
chmod +x "$work/runme"
