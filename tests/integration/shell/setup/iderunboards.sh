# Set up before iderunboards.script runs.  Sourced by run.sh with the
# harness's variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The board line names one that is NOT on this desk: what the editor
# does about that is part of what this script is for.
mkdir -p "$deskhome"
# The same two names, in the same order, as every other editor fixture:
# the Open panel's rows are then where these scripts already know.
printf 'title TwoBoards\nboard elsewhere\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
# The lamp is the evidence: one program puts it out, the other lights
# it, and afterwards each board's own /dev/led0 says which ran there.
printf '10 VFSWRITE "/dev/led0", 1\n' > "$deskhome/blink.bas"
printf '10 VFSWRITE "/dev/led0", 0\n' > "$deskhome/quiet.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" "$appsdir/programming/"
