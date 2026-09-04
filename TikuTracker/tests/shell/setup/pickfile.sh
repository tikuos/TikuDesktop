# Set up before pickfile.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A program for the editor to be given, where the shell's
# own panel starts looking: the home it opens on.  The
# home was wiped a few lines up and is not remade until
# further down, so this makes it rather than writing
# into a directory that is not there yet.
mkdir -p "$deskhome"
printf '10 PRINT "hi"\n' > "$deskhome/hello.bas"
