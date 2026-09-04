# Set up before idec.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A C project that names NO target: TikuC asks this
# machine's compiler about it anyway.
mkdir -p "$deskhome"
printf 'title Firmware\ngroup Sources\nfile bad.c\n' \
    > "$deskhome/project.tiku"
printf 'int main(void)\n{\n/* a remark */\nreturn nope;\n}\n' \
    > "$deskhome/bad.c"
mkdir -p "$deskhome/deep"
# More entries than the panel shows at once, so the
# scrollbar has something to be.
for zi in $(seq -w 1 20); do
    echo x > "$deskhome/zz$zi"
done
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuC.so" \
      "$appsdir/programming/"
