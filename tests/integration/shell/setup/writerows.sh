# TikuDesktop application tests.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Build a nested tall-row fixture in the isolated desktop home.
# SPDX-License-Identifier: Apache-2.0
cp -R "$top/build/runtime/apps/write" "$appsdir/"
mkdir -p "$deskhome/documents"
make -s -C "$top" build/tests/test_richrowoutput
"$top/build/tests/test_richrowoutput" "$deskhome/documents/tall-rows.tkrd"
