# Install only this lane's extra app; fixtures come from the repository.
cp -R "$top/build/runtime/apps/marker" "$appsdir/"
mkdir -p "$deskhome/markdown"
cp "$top/tests/fixtures/marker/readme.md" "$deskhome/markdown/a-readme.MD"
cp "$top/tests/fixtures/marker/details.markdown" \
   "$deskhome/markdown/b-details.MARKDOWN"
