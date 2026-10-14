#!/bin/sh
# Host checks of the firmware license question (plan/ws233/phase001/phase.md,
# section 4c) with the Raspberry Pi 4 configuration, which selects
# rtl8822b-firmware, brcmbt-firmware, bcm43455-firmware and needs the boot
# firmware.  Every case uses its own new cache roots below OUTDIR; the license
# texts and the firmware come from the network (the upstream, while the
# repository revision is empty).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: check-license-agree.sh FIRMWARE-REPO OUTDIR   (from the top of the tree)
# OUTDIR must not exist; nothing is removed.
set -eu

repo=$(cd "$1" && pwd)
out=$2
if test -e "$out"; then
	echo "check-license-agree: $out exists" >&2
	exit 1
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)
config=config/ci/config-rpi4.mk
fail=0

roots() {
	printf '%s\n' \
		"RTL8822B_FIRMWARE_CACHE_ROOT=$out/$1/firmware/rtl8822b" \
		"BRCMBT_FIRMWARE_CACHE_ROOT=$out/$1/firmware/brcmbt" \
		"BCM43455_FIRMWARE_CACHE_ROOT=$out/$1/firmware/bcm43455" \
		"RPI4_BOOT_FIRMWARE_CACHE_ROOT=$out/$1/firmware/rpi4-boot"
}

# mk CASE GOAL...: make with CASE's cache roots; the standard input is the
# caller's.  The status is saved in CASE.status.
mk() {
	case_name=$1
	shift
	set +e
	begin=$(date +%s)
	# shellcheck disable=SC2046
	make --no-print-directory -s ZEDBSD_CONFIG=$config $(roots "$case_name") "$@" \
		>"$out/$case_name.out" 2>"$out/$case_name.err"
	echo $? >"$out/$case_name.status"
	echo $(($(date +%s) - begin)) >"$out/$case_name.seconds"
	set -e
}

result() {
	if "$@"; then
		echo "OK   $label"
	else
		echo "FAIL $label"
		fail=1
	fi
}

asked() {
	grep -c 'Do you accept the license of' "$out/$1.err" || :
}

# 1. yes from a pipe: every selected package and the boot firmware are asked
#    once, in the order notice, copyright, license text, question.
yes | mk yes firmware-licenses
label="yes | make firmware-licenses: status 0, 4 questions, 4 records"
result test "$(cat "$out/yes.status")" = 0 -a "$(asked yes)" = 4 -a \
	"$(cat "$out"/yes/firmware/*/license-accepted | wc -l | tr -d ' ')" = 4
label="the display: Firmware:, Copyright, the license text, then the question"
result awk '/rpi4-boot-firmware: firmware license/{s=1} s&&/^Firmware:/{a=NR} s&&/Copyright \(c\) 2006, Broadcom/{if(!b)b=NR} s&&/^Redistribution\./{c=NR} s&&/Do you accept the license of rpi4-boot-firmware/{d=NR} END{exit !(a&&b&&c&&d&&a<b&&b<c&&c<d)}' "$out/yes.err"

# 2. The second time nothing is asked.
label="second run asks nothing"
# The second run uses the first run's roots.
set +e
# shellcheck disable=SC2046
make --no-print-directory -s ZEDBSD_CONFIG=$config $(roots yes) firmware-licenses \
	</dev/null >"$out/yes-again.out" 2>"$out/yes-again.err"
status=$?
set -e
result test "$status" = 0 -a "$(asked yes-again)" = 0

# 3. no: fails, nothing recorded, the message says what to do.
echo no | mk no firmware-licenses
label="echo no | make: fails, nothing recorded, says yes | make firmware-licenses"
result test "$(cat "$out/no.status")" != 0 -a ! -s "$out/no/firmware/bcm43455/license-accepted"
grep -q 'yes | make firmware-licenses' "$out/no.err" || { echo "FAIL no message"; fail=1; }

# 4. End of input (and an open pipe that never writes) fails at once.
mk eof firmware-licenses </dev/null
label="</dev/null: fails within 2 s"
result test "$(cat "$out/eof.status")" != 0 -a "$(cat "$out/eof.seconds")" -le 2
sleep 20 | mk silent firmware-licenses
label="an open silent pipe: make fails within 10 s (5 s for the answer)"
result test "$(cat "$out/silent.status")" != 0 -a "$(cat "$out/silent.seconds")" -le 10

# 5. yes | make -j4 fetching three packages at once: asked while make reads
#    the makefiles, so no job needs the standard input.
yes | mk jobs -j4 brcmbt-firmware bcm43455-firmware rpi4-boot-firmware
label="yes | make -j4 PKG-firmware...: status 0, 3 questions, 3 caches"
result test "$(cat "$out/jobs.status")" = 0 -a "$(asked jobs)" = 3 -a \
	"$(find "$out/jobs/firmware" -mindepth 2 -maxdepth 2 -type d ! -name licenses | wc -l | tr -d ' ')" = 3

# 6. Goals that do not need firmware never ask.
mk other list-targets </dev/null
label="make list-targets: no question, no record"
result test "$(cat "$out/other.status")" = 0 -a "$(asked other)" = 0 -a ! -e "$out/other"

# 7. A changed license text is asked about again.
mkdir -p "$out/changed/firmware/rpi4-boot"
printf '%s  %s\n' 0000000000000000000000000000000000000000000000000000000000000000 LICENCE.broadcom \
	>"$out/changed/firmware/rpi4-boot/license-accepted"
echo yes | mk changed rpi4-boot-firmware-license
label="a record of another text: asked again"
result test "$(asked changed)" -ge 1

# 8. The fetch recipe asks too (a fixture with the question on), from the
#    repository copy, and refuses on no without making the cache.
echo no | mk recipe-no INTELBT_FIRMWARE_LICENSE_PROMPT=y \
	INTELBT_FIRMWARE_ZEDBSD_URL="file://$repo/intelbt" \
	INTELBT_FIRMWARE_CACHE_ROOT="$out/recipe-no/intelbt" intelbt-firmware-fixture-cache
label="fixture recipe, no: fails, no cache"
result test "$(cat "$out/recipe-no.status")" != 0 -a "$(asked recipe-no)" = 1 -a \
	! -e "$out/recipe-no/intelbt/dc85ccedc9c973682fbcf4d628ca61174bcc3120"
yes | mk recipe-yes INTELBT_FIRMWARE_LICENSE_PROMPT=y \
	INTELBT_FIRMWARE_ZEDBSD_URL="file://$repo/intelbt" \
	INTELBT_FIRMWARE_URL_SUFFIX='?id=dc85ccedc9c973682fbcf4d628ca61174bcc3120' \
	INTELBT_FIRMWARE_CACHE_ROOT="$out/recipe-yes/intelbt" intelbt-firmware-fixture-cache
label="fixture recipe, yes: cache made"
result test "$(cat "$out/recipe-yes.status")" = 0 -a "$(asked recipe-yes)" = 1 -a \
	-d "$out/recipe-yes/intelbt/dc85ccedc9c973682fbcf4d628ca61174bcc3120"

test "$fail" = 0 && echo "check-license-agree: PASS" || { echo "check-license-agree: FAIL"; exit 1; }
