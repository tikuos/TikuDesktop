# Set up before bigcopy.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A file large enough that copying it takes the engine a while: sparse,
# so it costs no disk to make, and reads as zeros at memory speed.
truncate -s 1536M "$work/big.bin"
