# Set up before idemessages.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A program with a mistake ON A KNOWN LINE: what the
# interpreter blames is what the message must name.
mkdir -p "$deskhome"
printf 'title Blinker\ngroup Sources\nfile blink.bas\nfile quiet.bas\n' \
    > "$deskhome/project.tiku"
# A program that says NOTHING, to prove the message
# window is not made until there is something in it.
printf '10 VFSWRITE "/quiet", 1\n' > "$deskhome/quiet.bas"
# The mistake is DEEP in the file -- past what the
# window shows -- so following the message has to move
# the page as well as the caret.
# Line 20 leaves its line OPEN, so the error at 250 is
# written onto the end of it -- which is the shape a
# marker at the head of a row would miss.
{
    printf '10 PRINT "lamp"\n20 PRINT "value: ";\n'
    i=30
    while [ $i -le 240 ]; do
        printf '%d REM x\n' $i
        i=$((i + 10))
    done
    # The number 250 twice: storing a line REPLACES
    # one already numbered the same, so the SECOND is
    # what ran and the first was never in the program.
    printf '250 REM shadowed\n250 PRUNT "bad"\n260 PRINT "after"\n'
} > "$deskhome/blink.bas"
mkdir -p "$appsdir/programming"
cp -f "$top/build/apps/programming/TikuBasic.so" \
      "$appsdir/programming/"
