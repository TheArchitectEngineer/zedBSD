#!/bin/sh
# ws001: runs the utility difference cases on the lean amd64 guest.
#
#   sh plan/ws001/tests/guest-run.sh [OUTPUT] [CASE_FILE...]
#
# 1. exports the cases of plan/tools/utils/cases (all, or the named case
#    files such as xargs env) with the GNU reference outputs of the host;
# 2. builds the lean guest image (tests/config-amd64-lean-guest.mk) with
#    the cases and plan/ws001/tests/guest-cases.sh in /root;
# 3. boots it under QEMU from NVMe, logs in on the serial console
#    (plan/tools/guest/serial.py), runs the cases, and writes the FAIL
#    lines and the PASS count to OUTPUT (default build/ws001/guest.out);
# 4. stops the guest.
#
# The guest's runtime directory is build/ws001/guest-rt.  The host Noct of
# the main tree is used read-only for the image tools.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws001/guest.out}
if [ $# -gt 0 ]; then
	shift
fi
noct=${NOCT:-/home/awe/zedBSD-rpi4/build/NoctLang/build-static/noct}
export GUEST_RUNTIME=build/ws001/guest-rt

# The cases and their references.
rm -rf build/ws001/export
mkdir -p build/ws001
if [ $# -eq 0 ]; then
	python3 plan/tools/utils/util-diff.py --bin build/ws001/bin \
		--export build/ws001/export > /dev/null
else
	for name in "$@"; do
		python3 plan/tools/utils/util-diff.py --bin build/ws001/bin \
			--only "$name" --export "build/ws001/export-$name" > /dev/null
		mkdir -p build/ws001/export
		for group in "build/ws001/export-$name"/*; do
			for file in "$group"/*; do
				cp "$file" "build/ws001/export/$(basename "$group")-$name-$(basename "$file")"
			done
		done
		rm -rf "build/ws001/export-$name"
	done
	# guest-cases.sh reads GG/NNNN.*; one directory holds them all.
	mkdir -p build/ws001/export/00
	find build/ws001/export -maxdepth 1 -type f -exec mv {} build/ws001/export/00/ \;
fi
tar --format=ustar -C build/ws001/export -cf build/ws001/cases.tar .

# The image, with the cases and the runner.
make -j24 NOCT="$noct" ZEDBSD_CONFIG=plan/ws001/tests/config-amd64-lean-guest.mk \
	BUILD=build/ws001-guest \
	ZEDBSD_TEST_EXTRA_FILES='--file /root/ws001-cases.tar=build/ws001/cases.tar --file /root/guest-cases.sh=plan/ws001/tests/guest-cases.sh' \
	disk-image > build/ws001/guest-build.log 2>&1

# The guest, driven over its serial console.
python3 plan/tools/guest/guest.py stop > /dev/null 2>&1 || true
python3 plan/tools/guest/guest.py start --disk nvme build/ws001-guest/hdd-image.img
socket=$GUEST_RUNTIME/serial.sock
# QEMU makes the console socket a moment after it starts.
tries=0
while [ ! -S "$socket" ] && [ $tries -lt 30 ]; do
	sleep 1
	tries=$((tries + 1))
done
python3 plan/tools/guest/serial.py --socket "$socket" --timeout 300 expect 'login: '
python3 plan/tools/guest/serial.py --socket "$socket" login
python3 plan/tools/guest/serial.py --socket "$socket" run \
	'rm -rf /root/c && mkdir /root/c && cd /root/c && pax -r -f /root/ws001-cases.tar && cd /'
status=0
python3 plan/tools/guest/serial.py --socket "$socket" --timeout 7200 run \
	"DUMP=${DUMP-} sh /root/guest-cases.sh /root/c 2>&1 | grep -v '^$'" > "$out" || status=$?
python3 plan/tools/guest/guest.py stop > /dev/null 2>&1 || true
tail -1 "$out"
exit $status
