# Set up before fold.script runs.  Sourced by run.sh with the harness's
# variables in scope: $deskhome, $work, $appsdir, $top, $s.

# A C file whose first line opens a block of two lines.  Nothing else in
# the home, so the panel's first row is the file the script opens.
mkdir -p "$deskhome"
printf 'int main(void) {\n    one();\n    two();\n}\nint after;\n' \
    > "$deskhome/fold.c"
