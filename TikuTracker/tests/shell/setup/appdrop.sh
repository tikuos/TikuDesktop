# Set up before appdrop.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.
# An application that lives IN the folder, so a pose exists to
# drop documents onto; the registry names it as the Dropper.

printf '#!/bin/sh\nprintf "%%s\\n" "$@" > /tmp/trk_launched\n' \
    > "$work/zapp.sh"
chmod +x "$work/zapp.sh"
rm -f /tmp/trk_launched
mkdir -p "$deskhome/.config/tracker"
printf 'Dropper\t/tmp/trk_script/zapp.sh\ttext/plain\n' \
    > "$deskhome/.config/tracker/apps"
