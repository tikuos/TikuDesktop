# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Prepare a picture document for clipboard transfer between app processes.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richmedia
"$top/build/tests/test_richmedia" "$deskhome/documents/picture.tkrd"
