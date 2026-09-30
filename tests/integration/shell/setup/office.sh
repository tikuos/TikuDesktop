# A Word file, an OpenDocument one and a deck -- gen_office.py's, made
# fresh -- confined to the harness's fixture home.  TikuDoc and TikuSlides
# join this lane's applications only, so no other lane's menus change.
cp -R "$top/build/runtime/apps/doc" "$appsdir/"
cp -R "$top/build/runtime/apps/slides" "$appsdir/"
mkdir -p "$deskhome/office" "$work/office"
python3 "$top/tests/fixtures/tools/gen_office.py" "$work/office.inc" \
    "$work/office" > /dev/null
cp "$work/office/docx_features.docx" "$deskhome/office/features.docx"
cp "$work/office/odt_features.odt" "$deskhome/office/notes.odt"
cp "$work/office/pptx_features.pptx" "$deskhome/office/talk.pptx"
