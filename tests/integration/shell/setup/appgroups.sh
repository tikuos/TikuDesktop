# Install the shipped groups into this script's isolated application directory.
for name in notes marker doc pdf slides grab circuit calc archive basic c calculator hex diff bench console sdr hello clock counter sketch twowin sensors; do
    cp -R "$top/build/runtime/apps/$name" "$appsdir/"
done
