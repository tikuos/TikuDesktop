# Install the shipped groups into this script's isolated application directory.
for name in bench console hello clock counter sketch twowin sensors; do
    cp -R "$top/build/apps/$name" "$appsdir/"
done
