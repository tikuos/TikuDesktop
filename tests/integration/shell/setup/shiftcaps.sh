# Set up before shiftcaps.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A file for the editor to hold while Shift is proven:
# the capital lands IN the file, which is the one place
# a test can read the case back from.
mkdir -p "$deskhome"
printf 'x\n' > "$deskhome/caps.txt"
