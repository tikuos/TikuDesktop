# Set up before runbas.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A program whose effect is a FILE, because an effect is
# the one thing a test can hold: the run writes /led
# into the sandbox beside its source.
mkdir -p "$deskhome"
printf '10 PRINT "lamp goes on"\n20 VFSWRITE "/led", 7\n' \
    > "$deskhome/blink.bas"
