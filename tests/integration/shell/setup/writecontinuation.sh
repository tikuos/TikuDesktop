# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Build a long table with repeated headers in the isolated test home.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richcontinuation
"$top/build/tests/test_richcontinuation" "$deskhome/documents/report.tkrd"
