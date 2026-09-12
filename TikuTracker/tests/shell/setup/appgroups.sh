# Install the shipped groups into this script's isolated application directory.
for name in Bench TikuConsole hello clock counter sketch twowin sensors; do
    cp "$top/build/apps/$name.so" "$appsdir/"
done
