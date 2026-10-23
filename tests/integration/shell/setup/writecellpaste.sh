# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Populate a table for clipboard shape checks and undoable cell replacement.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richtables
"$top/build/tests/test_richtables" "$deskhome/documents/table.tkrd"
