# Install the preview bundle in this lane's isolated app registry.
cp -R "$top/build/runtime/apps/circuit" "$appsdir/"
mkdir -p "$deskhome"
cp "$top/tests/fixtures/circuit/basic.kicad_sch" "$deskhome/a-circuit.kicad_sch"
cp "$top/tests/fixtures/circuit/hierarchy.kicad_sch" "$deskhome/b-hierarchy.kicad_sch"
cp "$top/tests/fixtures/circuit/channel.kicad_sch" "$deskhome/channel.kicad_sch"
