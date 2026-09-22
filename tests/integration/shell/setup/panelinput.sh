# Set up before panelinput.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s, $tmp.

# A board whose places take input: words typed in, a number set by
# where a track is pressed, one word of a few.  The places are ordinary
# files outside the folder being shown.
mkdir -p "$tmp/trk_input"
printf 'workshop\n' > "$tmp/trk_input/name"
printf '50\n' > "$tmp/trk_input/bright"
printf 'slow\n' > "$tmp/trk_input/mode"
printf 'title Input\nentry Name %s/trk_input/name\nslider Brightness %s/trk_input/bright 0 100\nchoice Mode %s/trk_input/mode slow|fast\n' \
    "$tmp" "$tmp" "$tmp" > "$work/input.panel"
