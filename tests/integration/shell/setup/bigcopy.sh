# Set up before bigcopy.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# A file large enough that copying it takes the engine a while: sparse,
# so it costs no disk to make, and reads as zeros at memory speed.
# Through dd's seek rather than truncate(1), which is GNU coreutils and
# is not on every host; a seek past the end makes the same hole.
dd if=/dev/null of="$work/big.bin" bs=1 count=0 seek=1610612736 2>/dev/null
