#!/bin/sh
# ws206-p008: builds the WLAN common core (src/kern/net/wifi, with the FullMAC engine) with WLAN_TESTING and runs
#   1. fullmac-host-test.c under ASan and UBSan (the engine, attach, a FullMAC station's whole life, a SoftMAC
#      station refusing the FullMAC entry points);
#   2. the same with FULLMAC_TEST_THREADS under TSan (a data thread against the connection cycle);
#   3. the existing SoftMAC host test plan/ws005/tests/host-wlan-retire.c, unchanged, under ASan and UBSan, to show
#      the SoftMAC path still behaves as before.
# Each run's last line is "...: PASS".  Output goes to a fresh build/ws206-host/fullmac.run.* (fresh_out; Q1 removes
# old runs).  The core is built with warnings; the tests with -w because the kernel's uapi headers redefine the
# host's SIOC* numbers.
#   sh plan/ws206/tests/fullmac-host-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/fresh-out.sh
fresh_out build/ws206-host/fullmac
out=$fresh_dir
cc=${CC:-cc}
common="-std=gnu89 -O1 -g -DWLAN_TESTING -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -I. -Iinclude -Isrc -Isrc/kern/net/wifi"
core="wlan wlan-fullmac wlan-wpa2 wlan-wpa2-codec wlan-crypto wlan-frame wlan-l2"

# build VARIANT SANITIZER_FLAGS EXTRA_DEFINES TEST_SOURCE: compiles the core and one test into $out/VARIANT/test.
build() {
	variant=$1
	sanitize=$2
	defines=$3
	test_source=$4
	mkdir -p "$out/$variant"
	for file in $core; do
		"$cc" $common $sanitize $defines -Wall -Wextra -Wno-unused-parameter \
		    -c "src/kern/net/wifi/$file.c" -o "$out/$variant/$file.o" 2> "$out/$variant/$file.warnings" || {
			cat "$out/$variant/$file.warnings"
			exit 1
		}
		# The uapi SIOC* redefinitions are the host's, not the core's; anything else is a real warning.
		if grep -v 'SIOC\|redefined\|previous definition\|^In file included\|^ *|\|^ *[0-9]* |\|from \|^ *\^' \
		    "$out/$variant/$file.warnings" | grep -q .; then
			cat "$out/$variant/$file.warnings"
			echo "fullmac-host-test: warnings in $file.c"
			exit 1
		fi
	done
	"$cc" $common $sanitize $defines -w -c "$test_source" -o "$out/$variant/main.o"
	"$cc" $sanitize -o "$out/$variant/test" "$out/$variant"/*.o $5
}

asan="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
tsan="-fsanitize=thread"

build asan "$asan" "" plan/ws206/tests/fullmac-host-test.c ""
build tsan "$tsan" "-DFULLMAC_TEST_THREADS" plan/ws206/tests/fullmac-host-test.c "-pthread"
build retire "$asan" "" plan/ws005/tests/host-wlan-retire.c ""

status=0
for variant in asan tsan retire; do
	echo "== $variant"
	if ! TSAN_OPTIONS="halt_on_error=1" "$out/$variant/test" > "$out/$variant/log" 2>&1; then
		status=1
	fi
	tail -n 3 "$out/$variant/log"
done
echo "logs: $out"
exit $status
