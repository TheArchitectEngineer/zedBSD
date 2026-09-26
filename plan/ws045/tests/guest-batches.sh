#!/bin/sh
# ws045: runs the GNU cases (plan/ws045/tests/cases) on the amd64 guest with
# the utilities build-guest-utils.sh made, 40 at a time, starting the guest
# again every 10 batches (BUG-029), and writes each batch's FAIL lines and
# PASS count to OUTPUT; the last line is ALL-BATCHES-FINISHED.  With POSIX=1
# the WS043 POSIX cases (plan/tools/utils/cases, with POSIXLY_CORRECT on
# both sides) are run instead.
#   [IMAGE=...] [POSIX=1] sh plan/ws045/tests/guest-batches.sh [OUTPUT]
#   (default OUTPUT build/ws045/guest-all.out; IMAGE the native guest image
#   /home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img, booted
#   from NVMe, read only: the guest runs on a copy)
# With SH=1 the guest's copy also gets this tree's sh as /bin/sh and env as
# /usr/bin/env, so that the sh's env builtin (which leaves GNU's options to
# /usr/bin/env) is the one the cases run.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:-build/ws045/guest-all.out}
work=build/ws045/guest-batches
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/ws045/guest-run}
image=${IMAGE:-/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img}
guest="python3 plan/tools/guest/guest.py"

# The cases with the host's (GNU) output.
rm -rf "$work" build/ws045/guest-export
mkdir -p "$work"
if [ -n "${POSIX-}" ]; then
	python3 plan/tools/utils/util-diff.py --bin build/ws045/bin \
	    --export build/ws045/guest-export > /dev/null
else
	python3 plan/tools/utils/util-diff.py --bin build/ws045/bin \
	    --cases plan/ws045/tests/cases --gnu \
	    --export build/ws045/guest-export > /dev/null
fi

# The utilities, as one archive.
tar -C build/ws045 -cf "$work/bin.tar" guest-bin guest-bin.sh

# The cases, 40 to a batch.
ls build/ws045/guest-export/*/*.sh | sort > "$work/cases"
split -l 40 "$work/cases" "$work/batch."
: > "$out"
n=0
for batch in "$work"/batch.*; do
	if [ $((n % 10)) -eq 0 ]; then
		$guest stop >/dev/null 2>&1
		$guest start --disk nvme "$image" >/dev/null 2>&1
		$guest wait >/dev/null 2>&1
		$guest put plan/ws045/tests/guest-diff.sh /root/ws045-diff.sh
		$guest put "$work/bin.tar" /root/ws045-bin.tar
		$guest run 'cd /root && rm -rf guest-bin && pax -r -f ws045-bin.tar && rm -f ws045-bin.tar'
		if [ -n "${SH-}" ]; then
			$guest run 'cp /root/guest-bin.sh/sh /bin/sh.new && mv /bin/sh.new /bin/sh && cp /root/guest-bin/env /usr/bin/env.new && mv /usr/bin/env.new /usr/bin/env'
		fi
	fi
	n=$((n + 1))
	rm -rf "$work/one" && mkdir -p "$work/one/export/00"
	while read -r code; do
		cp "$code" "${code%.sh}.exp" "$work/one/export/00/"
	done < "$batch"
	tar -C "$work/one" -cf "$work/one.tar" export
	$guest put "$work/one.tar" /tmp/ws045b.tar
	if [ -n "${POSIX-}" ]; then
		$guest run "cd /tmp && pax -r -f ws045b.tar && rm -f ws045b.tar && POSIXLY_CORRECT=1 sh /root/ws045-diff.sh /tmp/export /root/guest-bin 2>&1; rm -rf /tmp/export" >> "$out" 2>&1
	else
		$guest run "cd /tmp && pax -r -f ws045b.tar && rm -f ws045b.tar && sh /root/ws045-diff.sh /tmp/export /root/guest-bin 2>&1; rm -rf /tmp/export" >> "$out" 2>&1
	fi
done
$guest stop >/dev/null 2>&1
echo ALL-BATCHES-FINISHED >> "$out"
