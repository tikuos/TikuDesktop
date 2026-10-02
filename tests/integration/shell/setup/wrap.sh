# Set up before wrap.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# One line of words far wider than the editor's window, and a short
# second line.  Nothing else in the home, so the panel's first row is
# the file the script opens.
mkdir -p "$deskhome"
words=""
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    words="${words}wrapped words $i "
done
printf '%s\nend\n' "$words" > "$deskhome/wrap.txt"
