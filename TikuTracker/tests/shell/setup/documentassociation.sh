# Confined to the shell harness's disposable home and application catalogue.
mkdir -p "$deskhome/documents" "$appsdir/programming"
printf 'Sensor notes\nSecond line\n' > "$deskhome/documents/a-notes.TXT"
printf '10 PRINT 9876\n20 END\n' > "$deskhome/documents/b-source.BAS"
printf 'title Association Test\ngroup Sources\nfile b-source.BAS\n' \
    > "$deskhome/documents/project.TIKU"
cp "$top/build/apps/editor.so" "$appsdir/"
cp "$top/build/apps/programming/TikuBasic.so" "$appsdir/programming/"
