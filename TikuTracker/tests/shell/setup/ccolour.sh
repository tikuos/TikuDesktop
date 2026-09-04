# Set up before ccolour.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The same words twice: once inside a block remark that
# was opened on an earlier line, once outside it.
mkdir -p "$deskhome"
printf '/* a remark that stays open\nint x = 42;\nstill inside */\nint x = 42;\n' \
    > "$deskhome/carry.c"
# Nothing else in the home, so the panel's first row is
# the file this script opens.
