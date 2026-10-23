# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Populate a long table for captured cell-drag scrolling.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richcontinuation
"$top/build/tests/test_richcontinuation" "$deskhome/documents/report.tkrd"
