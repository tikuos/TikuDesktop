# Two versions of a small C file, alone in the harness's home so the
# shell's panel lists them first and second.  TikuDiff joins this lane's
# applications only, so no other lane's menus change.
cp -R "$top/build/runtime/apps/diff" "$appsdir/"
mkdir -p "$deskhome"
printf 'int x = 1;\nint y;\n\nvoid f(void)\n{\n}\nint gone;\nint z;\n' \
    > "$deskhome/one.c"
printf 'int x = 2;\nint y;\n\nvoid g(void);\nvoid h(void);\n\n' \
    > "$deskhome/two.c"
printf 'void f(void)\n{\n}\nint z;\n' >> "$deskhome/two.c"
