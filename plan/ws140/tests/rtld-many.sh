#!/bin/sh
# ws140: runs rtld-many (rtld-many.c, the dynamic loader past its old fixed limits) in the running SSH guest.  Builds
# it and its 206 libraries with build-many.sh against BUILD's libc.so, sends them in one ustar archive, unpacks it
# with pax in /root/ws140 and runs the program there with LD_LIBRARY_PATH (dlopen with no requester reads no rpath).
# The guest runs BUILD's image (config-amd64-rtld.mk), so its /lib/ld.so is the loader under test.
# PASS: the last line rtld-many: PASS (the program printed RTLD-MANY: PASS); its output is in OUT/rtld-many.txt.
#   plan/tools/guest/test-image.sh plan/ws140/tests/config-amd64-rtld.mk BUILD
#   plan/tools/guest/guest.sh start BUILD/hdd-image.img
#   GUEST_RUNTIME=... plan/ws140/tests/rtld-many.sh [BUILD]          (default build/amd64; OUT is build/ws140-many)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
out=build/ws140-many
guest() { timeout 300 python3 plan/tools/guest/guest.py "$@" 2>&1 </dev/null; }

# The program and its libraries.
sh plan/ws140/tests/build-many.sh "$build" "$out/files" || { echo "rtld-many: FAIL (build)"; exit 1; }

# Into the guest, in one archive.
guest run 'rm -rf /root/ws140 /root/rtld-many.tar && mkdir -p /root/ws140' >/dev/null
guest put "$out/rtld-many.tar" /root/rtld-many.tar >/dev/null
guest run 'cd /root/ws140 && pax -r -f /root/rtld-many.tar && ls | wc -l' | tail -1 | sed 's/^/rtld-many: files in the guest: /'

# The run.
guest run 'cd /root/ws140 && chmod +x rtld-many && LD_LIBRARY_PATH=/root/ws140 /root/ws140/rtld-many; echo exit=$?' \
    > "$out/rtld-many.txt"
grep -E '^RTLD-MANY|^exit=' "$out/rtld-many.txt"
if grep -q '^RTLD-MANY: PASS' "$out/rtld-many.txt"; then
	echo "rtld-many: PASS"
	exit 0
fi
echo "rtld-many: FAIL"
exit 1
