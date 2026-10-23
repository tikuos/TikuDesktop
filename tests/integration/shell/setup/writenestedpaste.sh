# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Populate a nested table for cross-process rich clipboard transfer.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richcellclip
"$top/build/tests/test_richcellclip" "$deskhome/documents/nested.tkrd"
