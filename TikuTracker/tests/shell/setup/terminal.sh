# Set up before terminal.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# A program that paints one row at an ABSOLUTE position:
# written to a file because typing backslashes through
# the driver one key at a time proves nothing extra.
printf '#!/bin/sh\nprintf "\\033[12;1H\\033[7m%%80s\\033[0m" ""\n' \
    > "$tmp/trk_term_cup.sh"
chmod +x "$tmp/trk_term_cup.sh"
( HOME="$deskhome" "$top/build/tiku-term" \
      >/dev/null 2>&1 & )
