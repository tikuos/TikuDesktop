# Set up before notes.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# The application, as a flat module with its manifest beside it, so the
# Applications menu offers it under its own name.
cp -f "$top/build/runtime/apps/notes/TikuNotes.so" "$appsdir/"
cp -f "$top/apps/notes/notes.app" "$appsdir/TikuNotes.so.manifest"
# Two notes already on the desk, in the place the application keeps them
# under this home: one that matters, one that does not.  What is on disk
# is what comes back, and the lane reads the files the application writes.
mkdir -p "$deskhome/.local/share/notes"
printf '# tiku-note paper=Pink important=1\nBuy milk\nand eggs\n' \
    > "$deskhome/.local/share/notes/note-001.txt"
printf '# tiku-note paper=Yellow important=0\nGroceries\n' \
    > "$deskhome/.local/share/notes/note-002.txt"
