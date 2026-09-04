# Set up before devicepanel.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A board that ships NO interface code: it publishes a
# few lines saying what it has and where each thing
# lives, and the places themselves are ordinary files.
# The places live OUTSIDE the folder being shown, so the
# rows the script clicks stay the fixture's own.
mkdir -p /tmp/trk_board
printf '18\n' > /tmp/trk_board/temp
printf '0\n' > /tmp/trk_board/led
printf 'workshop\n' > /tmp/trk_board/name
printf 'title Board one\ntext Sensors and switches\ngauge Temperature /tmp/trk_board/temp 0 120\ntoggle Lamp /tmp/trk_board/led\nfield Name /tmp/trk_board/name\nhologram Face /nope\n' \
    > "$work/board.panel"
