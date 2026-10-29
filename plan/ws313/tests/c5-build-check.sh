#!/bin/sh
# The checks every change on the ODROID-C5's boot path goes through: the kernel of the C5 (text and Keiland), the
# Raspberry Pi 4 and 5 and the VisionFive 2 built with no warning, and the host tests of the C5's device tree, the
# meson MMC driver, the Mali driver's pieces and the Mali shader compiler.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws313/tests/c5-build-check.sh TAG   (from anywhere)
# Each build goes in build/TAG-<config>/ with its log beside it; the host tests' binaries in build/TAG-host/.  Every
# command has a time limit.  One line per check, then a last line ALL PASS or N FAIL.  Nothing is removed.
set -u
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tag=${1:?usage: c5-build-check.sh TAG}
cd "$repo" || exit 2
fails=0
host=build/$tag-host
mkdir -p "$host"

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

for config in odroid-c5 odroid-c5-keiland rpi4 rpi5 visionfive2; do
	out=build/$tag-$config
	mkdir -p "$out"
	log=$out/build.log
	if timeout 1800 make -j8 ZEDBSD_CONFIG=config/ci/config-$config.mk BUILD=$out $out/vmunix > "$log" 2>&1; then
		warnings=$(grep -cE ' warning:' "$log")
		if [ "$warnings" = 0 ]; then pass "vmunix $config"; else fail "vmunix $config: $warnings warnings ($log)"; fi
	else
		fail "vmunix $config: make failed ($log)"
	fi
done

# The device tree of the C5, built in the C5 text build's directory from the cache in build/dts-sources.
if timeout 600 make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk BUILD=build/$tag-odroid-c5 \
	ODROID_C5_DTS_CACHE_ROOT="$repo/build/dts-sources" odroid-c5-fdt-host-test > "$host/fdt.log" 2>&1; then
	pass "odroid-c5-fdt-host-test"
else
	fail "odroid-c5-fdt-host-test ($host/fdt.log)"
fi
dtb=build/$tag-odroid-c5/odroid-c5.dtb

cflags="-std=c11 -O2 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc"
if timeout 300 cc $cflags src/drivers/generic/fdt.c src/drivers/platform/s905x5m/s905x5m-soc.c \
	src/drivers/platform/s905x5m/s905x5m-mmc.c src/drivers/mmc/meson-mmc.c tests/meson-mmc-host-test.c \
	-o "$host/meson-mmc" > "$host/meson-mmc.log" 2>&1; then
	for mode in driver version1 "glue $dtb"; do
		# shellcheck disable=SC2086
		if timeout 120 "$host/meson-mmc" $mode >> "$host/meson-mmc.log" 2>&1; then pass "meson-mmc $mode"; else fail "meson-mmc $mode ($host/meson-mmc.log)"; fi
	done
else
	fail "meson-mmc build ($host/meson-mmc.log)"
fi

mali() {
	name=$1
	shift
	if timeout 300 cc $cflags "$@" -o "$host/$name" > "$host/$name.log" 2>&1 && timeout 120 "$host/$name" >> "$host/$name.log" 2>&1; then
		pass "mali $name: $(tail -n 1 "$host/$name.log")"
	else
		fail "mali $name ($host/$name.log)"
	fi
}
mali pagetable tests/mali-pagetable-host-test.c src/drivers/gpu/mali/mali-pagetable.c
mali firmware tests/mali-firmware-host-test.c src/drivers/gpu/mali/mali-firmware-image.c
mali interface tests/mali-interface-host-test.c src/drivers/gpu/mali/mali-interface.c src/drivers/gpu/mali/display/osd.c
mali cs tests/mali-cs-host-test.c src/drivers/gpu/mali/cs/cs-build.c
mali vk-desc tests/mali-vk-desc-host-test.c src/drivers/gpu/mali/vk/vk-desc.c
mali vk-frame tests/mali-vk-frame-host-test.c src/drivers/gpu/mali/vk/vk-frame.c src/drivers/gpu/mali/vk/vk-desc.c
mali scanout tests/mali-scanout-host-test.c src/drivers/gpu/mali/display/scanout.c
if timeout 300 cc -std=gnu11 -DKERN_USER_ABI_LP64 -Iinclude -Isrc -I. -idirafter libc/include -Wall -Wextra -Werror \
	tests/mali-preload-host-test.c src/drivers/gpu/mali/compiler/va-compile.c src/drivers/gpu/mali/compiler/va-encode.c \
	src/drivers/gpu/mali/compiler/va-schedule.c src/drivers/gpu/compiler/spirv.c src/drivers/gpu/compiler/optimize.c -lm \
	-o "$host/preload" > "$host/preload.log" 2>&1 && timeout 120 "$host/preload" "$host/preload.spv" >> "$host/preload.log" 2>&1; then
	pass "mali preload: $(tail -n 1 "$host/preload.log")"
else
	fail "mali preload ($host/preload.log)"
fi

if timeout 300 cc -std=gnu11 -DKERN_USER_ABI_LP64 -Iinclude -Isrc -I. -idirafter libc/include -Wall -Wextra -Werror \
	tests/mali-vk-meta-host-test.c src/drivers/gpu/mali/vk/vk-meta-spirv.c src/drivers/gpu/mali/compiler/va-compile.c \
	src/drivers/gpu/mali/compiler/va-encode.c src/drivers/gpu/mali/compiler/va-schedule.c src/drivers/gpu/compiler/spirv.c \
	src/drivers/gpu/compiler/optimize.c -lm -o "$host/meta" > "$host/meta.log" 2>&1 && timeout 120 "$host/meta" >> "$host/meta.log" 2>&1; then
	pass "mali meta: $(tail -n 1 "$host/meta.log")"
else
	fail "mali meta ($host/meta.log)"
fi

if timeout 1800 sh plan/ws313/tests/mali-compiler-host-test.sh 50 > "$host/compiler.log" 2>&1; then
	pass "mali compiler: $(grep -c '^PASS' "$host/compiler.log") PASS lines"
else
	fail "mali compiler ($host/compiler.log)"
fi
if timeout 1800 sh plan/ws313/tests/mali-compiler-desktop-test.sh 50 > "$host/compiler-desktop.log" 2>&1; then
	pass "mali compiler desktop: $(tail -n 1 "$host/compiler-desktop.log")"
else
	fail "mali compiler desktop ($host/compiler-desktop.log)"
fi

if [ "$fails" = 0 ]; then echo "ALL PASS"; else echo "$fails FAIL"; fi
[ "$fails" = 0 ]
