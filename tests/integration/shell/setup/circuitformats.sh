# Isolated registry and authored extended-format fixtures.
cp -R "$top/build/runtime/apps/circuit" "$appsdir/"
mkdir -p "$deskhome/formats"
cp "$top/tests/fixtures/circuit/eagle.sch" "$deskhome/formats/a-eagle.SCH"
cp "$top/tests/fixtures/circuit/macros.gbr" "$deskhome/formats/b-macros.gbr"
cp "$top/tests/fixtures/circuit/repeats.gbr" "$deskhome/formats/c-repeats.gbr"
cp "$top/tests/fixtures/circuit/cutins.gbr" "$deskhome/formats/d-cutins.gbr"
