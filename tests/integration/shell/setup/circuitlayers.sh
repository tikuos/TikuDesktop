# Each automatic dependency stays within this lane's selected job directory.
cp -R "$top/build/runtime/apps/circuit" "$appsdir/"
mkdir -p "$deskhome/layers"
for name in stack.gbrjob partial.gbrjob artwork.gbr polarity.gbr drills.drl; do
    cp "$top/tests/fixtures/circuit/$name" "$deskhome/layers/$name"
done
