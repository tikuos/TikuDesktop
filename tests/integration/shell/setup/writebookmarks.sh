# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Prepare named destinations and an internal link in the isolated home.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richbookmarksio
"$top/build/tests/test_richbookmarksio" "$deskhome/documents/bookmarks.tkrd"
