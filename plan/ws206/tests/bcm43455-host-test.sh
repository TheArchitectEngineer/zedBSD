#!/bin/sh
#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# Builds the BCM43455 driver's host test (ws206-p004, ws206-p005) twice and
# runs it: the unit and radio scenarios under AddressSanitizer and
# UndefinedBehaviorSanitizer, the smp and radio-smp scenarios under
# ThreadSanitizer.  The guest is not started.
#
#   sh plan/ws206/tests/bcm43455-host-test.sh OUT [FILES]
#
# OUT is a directory of the caller's own, such as build/ws206-p004/bcm43455.
# FILES, if given, is the bcm43455-firmware package's verified cache (the
# directory holding cyfmac43455-sdio-minimal.bin, the CLM and the NVRAM);
# the real files are then checked and converted too.

set -eu

REPO=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:?usage: bcm43455-host-test.sh OUT [FILES]}
FILES=${2:-}
CC=${CC:-cc}

mkdir -p "$OUT"

DRIVER=$REPO/src/drivers/wifi/bcm43455

# The driver's files that need no network header, compiled with every warning.
STRICT="$DRIVER/bcm43455-bcdc.c
$DRIVER/bcm43455-bus.c
$DRIVER/bcm43455-chip.c
$DRIVER/bcm43455-event.c
$DRIVER/bcm43455-nvram.c
$DRIVER/bcm43455-radio.c
$DRIVER/bcm43455-sdpcm.c"

# The device, its radio operations, the WLAN core's frame parser the test
# checks scan results with, and the test include both the network device and the WLAN
# headers, whose UAPI request numbers the host's <sys/ioctl.h> also
# defines; the host compiler's redefinition notes cannot be turned off
# one by one, so these two are compiled with -w.  The kernel's own build
# compiles bcm43455.c and bcm43455-station.c with -Wall -Wextra -Werror.
QUIET="$REPO/plan/ws206/tests/bcm43455-host-test.c
$DRIVER/bcm43455.c
$DRIVER/bcm43455-station.c
$REPO/src/kern/net/wifi/wlan-frame.c"

# The test's thread header stands in for the kernel's, which needs the HAL.
INCLUDES="-I$REPO/plan/ws206/tests/stub -I$REPO/include -I$REPO/src -I$REPO"

# Builds one flavour: $1 is its name, the rest the sanitizer flags.
build() {
	name=$1
	shift
	mkdir -p "$OUT/$name"
	objects=
	for source in $STRICT; do
		object=$OUT/$name/$(basename "$source" .c).o
		# shellcheck disable=SC2086
		"$CC" -D_GNU_SOURCE $INCLUDES -std=c11 -O1 -g -Wall -Wextra -Werror "$@" -pthread -c "$source" -o "$object"
		objects="$objects $object"
	done
	for source in $QUIET; do
		object=$OUT/$name/$(basename "$source" .c).o
		# shellcheck disable=SC2086
		"$CC" -D_GNU_SOURCE $INCLUDES -std=c11 -O1 -g -w "$@" -pthread -c "$source" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$CC" "$@" -pthread $objects -o "$OUT/bcm43455-host-test-$name"
}

build unit -fsanitize=address,undefined -fno-sanitize-recover=undefined
build smp -fsanitize=thread

# Any ThreadSanitizer report fails the run.
TSAN_OPTIONS="halt_on_error=1 exitcode=66 ${TSAN_OPTIONS:-}"
export TSAN_OPTIONS

if [ -n "$FILES" ]; then
	"$OUT/bcm43455-host-test-unit" unit "$FILES"
else
	"$OUT/bcm43455-host-test-unit" unit
fi
"$OUT/bcm43455-host-test-smp" smp
"$OUT/bcm43455-host-test-unit" radio
"$OUT/bcm43455-host-test-smp" radio-smp
echo "bcm43455-host-test: PASS"
