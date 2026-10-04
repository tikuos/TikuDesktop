# Modern KiCad routing in this lane's isolated registry and home.
cp -R "$top/build/runtime/apps/circuit" "$appsdir/"
mkdir -p "$deskhome/circuits"
cp "$top/tests/fixtures/circuit/labels.kicad_sch" "$deskhome/circuits/a-labels.KICAD_SCH"
cp "$top/tests/fixtures/circuit/properties.kicad_sch" "$deskhome/circuits/b-properties.kicad_sch"
