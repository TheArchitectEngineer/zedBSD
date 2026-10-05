#!/bin/sh
# ws066-p001: the start times of dynamic programs in the running guest, to split the loader's part from the rest.
# Builds the programs with build-measure.sh against BUILD (the build of the guest's image), sends them in one
# archive to /root/ws066, and runs startbench there (each line is the median of COUNT starts after a warm-up):
#   true-static, true-sysv, true-gnu        the empty program: static, dynamic as the base programs, with a GNU hash
#   true-sysv+bsymf, true-gnu+bsymf         the same against the libc.so linked with -Bsymbolic-functions
#   /bin/true, sh -c :                      the image's own (and sh against the -Bsymbolic-functions libc.so)
#   clang --version, cc t.c -o t            when the image has clang (the full image, config-amd64-full.mk)
# The output is OUT/startup.txt; the lines that matter are STARTBENCH ... median=N us.  QEMU numbers, not hardware.
#   plan/tools/guest/build-full-image.sh BUILD; plan/tools/guest/guest.sh start BUILD/hdd-image.img
#   GUEST_RUNTIME=... plan/ws066/tests/startup-measure.sh [BUILD]      (default build/amd64; OUT is build/ws066-p001)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
out=build/ws066-p001
guest() { timeout 600 python3 plan/tools/guest/guest.py "$@" 2>&1 </dev/null; }

# The programs, into the guest.
sh plan/ws066/tests/build-measure.sh "$build" "$out/files" || { echo "startup-measure: FAIL (build)"; exit 1; }
guest run 'rm -rf /root/ws066 /root/measure.tar && mkdir -p /root/ws066' >/dev/null
guest put "$out/measure.tar" /root/measure.tar >/dev/null
guest run 'cd /root/ws066 && pax -r -f /root/measure.tar && chmod +x startbench true-static true-sysv true-gnu' >/dev/null

# The measurements, one guest command.
guest run 'cd /root/ws066 && b=./startbench && alt=LD_LIBRARY_PATH=/root/ws066/bsymf &&
$b 2000 true-static ./true-static &&
$b 2000 true-sysv ./true-sysv &&
$b 2000 true-gnu ./true-gnu &&
env $alt $b 2000 true-sysv+bsymf ./true-sysv &&
env $alt $b 2000 true-gnu+bsymf ./true-gnu &&
$b 2000 bin-true /bin/true &&
$b 2000 sh-c /bin/sh -c : &&
env $alt $b 2000 sh-c+bsymf /bin/sh -c : &&
if [ -x /usr/bin/clang ]; then
	$b 100 clang-version /usr/bin/clang --version &&
	env $alt $b 100 clang-version+bsymf /usr/bin/clang --version &&
	$b 30 cc-t /usr/bin/cc t.c -o t &&
	env $alt $b 30 cc-t+bsymf /usr/bin/cc t.c -o t
else
	echo "STARTBENCH no clang in the image"
fi; echo exit=$?' > "$out/startup.txt"
grep -E '^STARTBENCH|^exit=' "$out/startup.txt"
if grep -q '^exit=0' "$out/startup.txt"; then
	echo "startup-measure: done"
	exit 0
fi
echo "startup-measure: FAIL"
exit 1
