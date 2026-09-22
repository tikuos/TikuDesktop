# Set up before ideprojicon.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# Two folders side by side: one IS a project, one is
# merely a folder, and the panel must say which.
mkdir -p "$deskhome/aproj" "$deskhome/bplain"
printf 'title aproj\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/aproj/project.tiku"
: > "$deskhome/aproj/main.bas"
printf '10 rem stray\n' > "$deskhome/stray.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
