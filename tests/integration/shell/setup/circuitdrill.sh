# This lane owns its registry and drill-only Tracker folder.
cp -R "$top/build/runtime/apps/circuit" "$appsdir/"
mkdir -p "$deskhome/drills"
cp "$top/tests/fixtures/circuit/drills.drl" "$deskhome/drills/a-drills.DRL"
cp "$top/tests/fixtures/circuit/routes.xnc" "$deskhome/drills/b-routes.xnc"
