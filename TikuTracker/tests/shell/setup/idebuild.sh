# Set up before idebuild.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A project that says what it is FOR, and C with a
# mistake on a line the compiler will name.
mkdir -p "$deskhome"
printf 'title Firmware\ntarget host\ngroup Sources\nfile bad.c\n' \
    > "$deskhome/project.tiku"
# No indent on the line that will be blamed: "go to
# line" means its FIRST column, and a needle with a run
# of spaces in it cannot be written in a script.
printf 'int main(void)\n{\n/* a remark */\nreturn nope;\n}\n' \
    > "$deskhome/bad.c"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
