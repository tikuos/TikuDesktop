# Set up before idehscroll.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

mkdir -p "$deskhome"
printf 'title Wide\ngroup Sources\nfile main.bas\n' \
    > "$deskhome/project.tiku"
long="aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu vvvv wwww xxxx yyyy"
printf '10 rem head\n20 print "%s Zz"\n' "$long" \
    > "$deskhome/main.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
