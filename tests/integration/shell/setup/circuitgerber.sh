# This lane owns its registry and artwork-only Tracker folder.
cp -R "$top/build/runtime/apps/circuit" "$appsdir/"
mkdir -p "$deskhome/artwork"
cp "$top/tests/fixtures/circuit/artwork.gbr" "$deskhome/artwork/a-artwork.GBR"
cp "$top/tests/fixtures/circuit/polarity.gbr" "$deskhome/artwork/b-polarity.gtl"
