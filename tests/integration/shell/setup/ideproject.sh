# Set up before ideproject.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A project is a FOLDER: the description, and the files
# it names, beside each other where the panel opens.
# Where the panel opens, so the description is picked
# in one gesture; the project's directory IS its folder.
mkdir -p "$deskhome"
printf 'title Blinker\ngroup Sources\nfile blink.bas\nfile util.bas\ntarget rp2350\n' \
    > "$deskhome/project.tiku"
printf '10 PRINT "lamp"\n' > "$deskhome/blink.bas"
printf '20 REM helpers\n' > "$deskhome/util.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/runtime/apps/basic/TikuBasic.so" \
      "$appsdir/programming/"
