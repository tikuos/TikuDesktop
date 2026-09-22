# Binary image fixtures; uppercase suffixes exercise the MIME associations.
mkdir -p "$deskhome/images"
. "$here/setup/imageviewer.sh"
for name in a.PNG b.JPG c.JPEG d.BMP; do
    cp "$deskhome/snapshot.bmp" "$deskhome/images/$name"
done
