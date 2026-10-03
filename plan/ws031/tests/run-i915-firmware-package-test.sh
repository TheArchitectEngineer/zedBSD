#!/usr/bin/env bash
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/zedbsd-i915-firmware.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT

for command_name in awk curl find git grep make mktemp python3 sha256sum wc; do
	command -v "$command_name" >/dev/null || {
		echo "missing host command: $command_name" >&2
		exit 1
	}
done

package_makefile=$repo/userland/firmware/i915/Makefile
manifest=$repo/userland/firmware/i915/i915-firmware.manifest
absent_config=$temporary/absent-config.mk
fetch_log=$temporary/fetch.log
fetcher=$temporary/fetch
fail_fetcher=$temporary/fail-fetch

printf '%s\n' \
	'#!/bin/sh' \
	'set -eu' \
	': "${FETCH_LOG:?}"' \
	'printf "%s\n" "$*" >>"$FETCH_LOG"' \
	'exec curl --fail --location --silent --show-error "$@"' >"$fetcher"
printf '%s\n' \
	'#!/bin/sh' \
	'echo "unexpected firmware network fetch" >&2' \
	'exit 97' >"$fail_fetcher"
chmod +x "$fetcher" "$fail_fetcher"

if git -C "$repo" ls-files | grep -E \
	'(^|/)(adlp_dmc|tgl_dmc_ver2_12)\.bin$' \
	>/dev/null; then
	echo 'i915 DMC firmware bytes must not be tracked' >&2
	exit 1
fi

# Static package metadata and the manifest must describe the exact DMC bytes
# the i915 driver was brought up with (the former embedded reference blobs,
# removed from the kernel on 2026-09-22; the driver now reads these files),
# the official immutable linux-firmware commit, and the complete
# LICENSE.i915/WHENCE.
grep -Fq 'override I915_FIRMWARE_REVISION := dc85ccedc9c973682fbcf4d628ca61174bcc3120' "$package_makefile"
grep -Fq 'override I915_FIRMWARE_URL_SUFFIX := ?id=dc85ccedc9c973682fbcf4d628ca61174bcc3120' "$package_makefile"
grep -Fq 'acquisition-tag=20260410' "$manifest"
grep -Fq 'acquisition-tag-object=4585dd5a5f0cee08990d754701d8866d9e9266e6' "$manifest"
grep -Fq 'acquisition-revision=dc85ccedc9c973682fbcf4d628ca61174bcc3120' "$manifest"
grep -Fq 'acquisition-adlp-dmc-path=i915/adlp_dmc.bin' "$manifest"
grep -Fq 'acquisition-adlp-dmc-size=79088' "$manifest"
grep -Fq 'acquisition-adlp-dmc-sha256=3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb' "$manifest"
grep -Fq 'acquisition-tgl-dmc-path=i915/tgl_dmc_ver2_12.bin' "$manifest"
grep -Fq 'acquisition-tgl-dmc-size=19760' "$manifest"
grep -Fq 'acquisition-tgl-dmc-sha256=3c013ef0ad96ba73aee8e5bd04a8e27cc9b1c6e9183b1a83ce124485f325afca' "$manifest"
grep -Fq 'acquisition-license-path=LICENSE.i915' "$manifest"
grep -Fq 'acquisition-license-size=2080' "$manifest"
grep -Fq 'acquisition-license-sha256=8542aeabf2761935122d693561e16766ce1bcc2b0d003204f9040b7d6d929f2e' "$manifest"
grep -Fq 'acquisition-whence-size=425450' "$manifest"
grep -Fq 'acquisition-whence-sha256=c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527' "$manifest"
grep -Fq 'adlp-dmc-version=2.20' "$manifest"
grep -Fq 'tgl-dmc-version=2.12' "$manifest"
grep -Fq 'redistribution=unmodified-binary-only' "$manifest"

# The kernel carries no DMC bytes (LICENSE.i915 firmware stays out of it):
# the driver reads /lib/firmware/i915/*.bin, which this package installs.
if grep -rlE 'drv_i915_firmware_(adlp|tgl)_dmc' "$repo/src/drivers/gpu/i915" \
	--include='*.c' --include='*.h' --include='*.inc' >/dev/null; then
	echo 'i915 DMC firmware bytes must not be embedded in the kernel' >&2
	exit 1
fi

# Production acquisition identity, accepted bytes, verifier, manifest, and
# rootfs mapping must ignore command-line substitutions.
locked_metadata=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=amd64 \
	ZEDBSD_ARCHITECTURE=amd64 ZEDBSD_BOARD=pcat ZEDBSD_VARIANT=uefi \
	I915_FIRMWARE_BASE_URL=https://example.invalid/attacker \
	I915_FIRMWARE_REVISION=attacker \
	I915_FIRMWARE_URL_SUFFIX='?id=attacker' \
	I915_FIRMWARE_ADLP_DMC_SIZE=1 \
	I915_FIRMWARE_ADLP_DMC_SHA256=deadbeef \
	I915_FIRMWARE_TGL_DMC_SIZE=1 \
	I915_FIRMWARE_TGL_DMC_SHA256=deadbeef \
	I915_FIRMWARE_LICENSE_SIZE=1 \
	I915_FIRMWARE_LICENSE_SHA256=deadbeef \
	I915_FIRMWARE_WHENCE_SIZE=1 \
	I915_FIRMWARE_WHENCE_SHA256=deadbeef \
	I915_FIRMWARE_FETCH=false \
	I915_FIRMWARE_SHA256_COMMAND=false \
	I915_FIRMWARE_MANIFEST=/tmp/attacker-manifest \
	I915_FIRMWARE_DATA=/tmp/attacker-data \
	'USERLAND_i915-firmware_DATA=/tmp/attacker-userland-data' \
	--eval='i915fw-print-locked-metadata:;@printf "%s\n" "$(I915_FIRMWARE_BASE_URL)|$(I915_FIRMWARE_REVISION)|$(I915_FIRMWARE_URL_SUFFIX)|$(I915_FIRMWARE_ADLP_DMC_SIZE)|$(I915_FIRMWARE_ADLP_DMC_SHA256)|$(I915_FIRMWARE_TGL_DMC_SIZE)|$(I915_FIRMWARE_TGL_DMC_SHA256)|$(I915_FIRMWARE_LICENSE_SIZE)|$(I915_FIRMWARE_LICENSE_SHA256)|$(I915_FIRMWARE_WHENCE_SIZE)|$(I915_FIRMWARE_WHENCE_SHA256)|$(I915_FIRMWARE_FETCH)|$(I915_FIRMWARE_SHA256_COMMAND)|$(I915_FIRMWARE_MANIFEST)|$(I915_FIRMWARE_DATA)|$(USERLAND_i915-firmware_DATA)"' \
	i915fw-print-locked-metadata)
grep -Fq 'https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain|dc85ccedc9c973682fbcf4d628ca61174bcc3120|?id=dc85ccedc9c973682fbcf4d628ca61174bcc3120|79088|3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb|19760|3c013ef0ad96ba73aee8e5bd04a8e27cc9b1c6e9183b1a83ce124485f325afca|2080|8542aeabf2761935122d693561e16766ce1bcc2b0d003204f9040b7d6d929f2e|425450|c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527|curl --fail --location --silent --show-error|sha256sum|userland/firmware/i915/i915-firmware.manifest|' <<<"$locked_metadata"
grep -Fq '/lib/firmware/i915/adlp_dmc.bin=' <<<"$locked_metadata"
grep -Fq '/lib/firmware/i915/tgl_dmc_ver2_12.bin=' <<<"$locked_metadata"
if grep -Eq 'attacker|deadbeef|/tmp/attacker|\|false\|' <<<"$locked_metadata"; then
	echo 'production i915 metadata accepted a command-line substitution' >&2
	exit 1
fi

# Package discovery and default configuration are metadata-only and cannot
# select or acquire the firmware.
default_cache=$temporary/default-cache
default_config=$temporary/default.mk
make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	I915_FIRMWARE_CACHE_ROOT="$default_cache" \
	list-user-programs >"$temporary/programs"
grep -Fq 'i915-firmware|Intel i915 display DMC firmware|amd64|n|firmware|firmware/i915|' "$temporary/programs"
ZEDBSD_CONFIG="$absent_config" \
	python3 "$repo/tools/menuconfig.py" --defaults --output "$default_config"
if grep '^ZEDBSD_USER_PROGRAMS' "$default_config" | \
	grep -Fq 'i915-firmware'; then
	echo 'i915 firmware must be default-off' >&2
	exit 1
fi
test ! -e "$default_cache"
default_data=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$default_config" \
	--eval='i915fw-print-default-data:;@printf "%s\n" "$(ZEDBSD_USERLAND_DATA_INPUTS)"' \
	i915fw-print-default-data)
if grep -Eq 'adlp_dmc|tgl_dmc' <<<"$default_data"; then
	echo 'ordinary default image depends on i915 firmware' >&2
	exit 1
fi
test ! -e "$default_cache"

# The fixture override exists only for its single hermetic goal.
if make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	i915-firmware-fixture-cache list-user-programs \
	>"$temporary/mixed-goal.log" 2>&1; then
	echo 'fixture goal unexpectedly accepted a second goal' >&2
	exit 1
fi
grep -Fq 'must be the only requested goal' "$temporary/mixed-goal.log"

revision=i915fw-fixture-revision
fixture=$temporary/mirror
mkdir -p "$fixture/i915"
printf 'i915fw ADL-P DMC fixture\n' \
	>"$fixture/i915/adlp_dmc.bin"
printf 'i915fw TGL DMC fixture\n' \
	>"$fixture/i915/tgl_dmc_ver2_12.bin"
printf 'i915fw Intel firmware license fixture\n' \
	>"$fixture/LICENSE.i915"
printf 'i915fw WHENCE fixture\n' \
	>"$fixture/WHENCE"

adlp_size=$(wc -c <"$fixture/i915/adlp_dmc.bin" | tr -d '[:space:]')
tgl_size=$(wc -c <"$fixture/i915/tgl_dmc_ver2_12.bin" | tr -d '[:space:]')
license_size=$(wc -c <"$fixture/LICENSE.i915" | tr -d '[:space:]')
whence_size=$(wc -c <"$fixture/WHENCE" | tr -d '[:space:]')
adlp_hash=$(sha256sum "$fixture/i915/adlp_dmc.bin" | awk '{print $1}')
tgl_hash=$(sha256sum "$fixture/i915/tgl_dmc_ver2_12.bin" | awk '{print $1}')
license_hash=$(sha256sum "$fixture/LICENSE.i915" | awk '{print $1}')
whence_hash=$(sha256sum "$fixture/WHENCE" | awk '{print $1}')

run_package() {
	local cache_root=$1
	local fetch_command=$2
	FETCH_LOG=$fetch_log make -C "$repo" --no-print-directory \
		ZEDBSD_CONFIG="$absent_config" \
		I915_FIRMWARE_BASE_URL="file://$fixture" \
		I915_FIRMWARE_REVISION="$revision" \
		I915_FIRMWARE_URL_SUFFIX= \
		I915_FIRMWARE_ADLP_DMC_SIZE="$adlp_size" \
		I915_FIRMWARE_ADLP_DMC_SHA256="$adlp_hash" \
		I915_FIRMWARE_TGL_DMC_SIZE="$tgl_size" \
		I915_FIRMWARE_TGL_DMC_SHA256="$tgl_hash" \
		I915_FIRMWARE_LICENSE_SIZE="$license_size" \
		I915_FIRMWARE_LICENSE_SHA256="$license_hash" \
		I915_FIRMWARE_WHENCE_SIZE="$whence_size" \
		I915_FIRMWARE_WHENCE_SHA256="$whence_hash" \
		I915_FIRMWARE_CACHE_ROOT="$cache_root" \
		I915_FIRMWARE_FETCH="$fetch_command" \
		i915-firmware-fixture-cache
}

cache_root=$temporary/cache
: >"$fetch_log"
run_package "$cache_root" "$fetcher"
cache=$cache_root/$revision
test "$(wc -l <"$fetch_log" | tr -d '[:space:]')" = 4
test "$(sha256sum "$cache/adlp_dmc.bin" | awk '{print $1}')" = "$adlp_hash"
test "$(sha256sum "$cache/tgl_dmc_ver2_12.bin" | awk '{print $1}')" = "$tgl_hash"
test "$(sha256sum "$cache/LICENSE.i915" | awk '{print $1}')" = "$license_hash"
test "$(sha256sum "$cache/WHENCE" | awk '{print $1}')" = "$whence_hash"

# A complete cache is reusable offline.  A missing cache, unsafe path,
# unexpected file, or any independently corrupt object must fail without
# silently replacing existing state.
: >"$fetch_log"
run_package "$cache_root" "$fail_fetcher"
test ! -s "$fetch_log"

missing_root=$temporary/missing
if run_package "$missing_root" "$fail_fetcher" \
	>"$temporary/missing.log" 2>&1; then
	echo 'missing offline i915 cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unexpected firmware network fetch' "$temporary/missing.log"
test ! -e "$missing_root/$revision"

snapshot=$temporary/snapshot
cp -a "$cache" "$snapshot"
for corrupt_name in \
	adlp_dmc.bin \
	tgl_dmc_ver2_12.bin \
	LICENSE.i915 WHENCE; do
	corrupt_root=$temporary/corrupt-$corrupt_name
	mkdir -p "$corrupt_root"
	cp -a "$snapshot" "$corrupt_root/$revision"
	printf 'corrupt\n' >>"$corrupt_root/$revision/$corrupt_name"
	if run_package "$corrupt_root" "$fail_fetcher" \
		>"$temporary/corrupt.log" 2>&1; then
		echo "corrupt i915 cache unexpectedly accepted: $corrupt_name" >&2
		exit 1
	fi
	grep -Eq 'size mismatch|SHA-256 mismatch' "$temporary/corrupt.log"
done

extra_root=$temporary/extra
mkdir -p "$extra_root"
cp -a "$snapshot" "$extra_root/$revision"
: >"$extra_root/$revision/unexpected"
if run_package "$extra_root" "$fail_fetcher" \
	>"$temporary/extra.log" 2>&1; then
	echo 'i915 cache with an extra object unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'cache must contain exactly four files' "$temporary/extra.log"

unsafe_root=$temporary/unsafe
mkdir -p "$unsafe_root"
ln -s "$snapshot" "$unsafe_root/$revision"
if run_package "$unsafe_root" "$fail_fetcher" \
	>"$temporary/unsafe.log" 2>&1; then
	echo 'symlink i915 cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unsafe cache path' "$temporary/unsafe.log"

# A partial fetch is cleaned without publishing the revision directory.
partial_fixture=$temporary/partial-mirror
partial_root=$temporary/partial-cache
mkdir -p "$partial_fixture/i915"
cp "$fixture/i915/adlp_dmc.bin" \
	"$partial_fixture/i915/"
if FETCH_LOG=$fetch_log make -C "$repo" --no-print-directory \
	ZEDBSD_CONFIG="$absent_config" \
	I915_FIRMWARE_BASE_URL="file://$partial_fixture" \
	I915_FIRMWARE_REVISION="$revision" \
	I915_FIRMWARE_URL_SUFFIX= \
	I915_FIRMWARE_ADLP_DMC_SIZE="$adlp_size" \
	I915_FIRMWARE_ADLP_DMC_SHA256="$adlp_hash" \
	I915_FIRMWARE_TGL_DMC_SIZE="$tgl_size" \
	I915_FIRMWARE_TGL_DMC_SHA256="$tgl_hash" \
	I915_FIRMWARE_LICENSE_SIZE="$license_size" \
	I915_FIRMWARE_LICENSE_SHA256="$license_hash" \
	I915_FIRMWARE_WHENCE_SIZE="$whence_size" \
	I915_FIRMWARE_WHENCE_SHA256="$whence_hash" \
	I915_FIRMWARE_CACHE_ROOT="$partial_root" \
	I915_FIRMWARE_FETCH="$fetcher" \
	i915-firmware-fixture-cache >"$temporary/partial.log" 2>&1; then
	echo 'partial i915 acquisition unexpectedly succeeded' >&2
	exit 1
fi
test ! -e "$partial_root/$revision"
test -z "$(find "$partial_root" -mindepth 1 -print -quit)"

# The selected rootfs mapping installs exactly two device files plus the full
# Intel license, full WHENCE, and static provenance manifest.
data_mapping=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=amd64 \
	ZEDBSD_ARCHITECTURE=amd64 ZEDBSD_BOARD=pcat ZEDBSD_VARIANT=uefi \
	ZEDBSD_USER_PROGRAMS=i915-firmware \
	--eval='i915fw-print-firmware-data:;@printf "%s\n" "$(I915_FIRMWARE_DATA)"' \
	i915fw-print-firmware-data)
grep -Fq '/lib/firmware/i915/adlp_dmc.bin=' <<<"$data_mapping"
grep -Fq '/lib/firmware/i915/tgl_dmc_ver2_12.bin=' <<<"$data_mapping"
grep -Fq '/usr/share/licenses/i915-firmware/LICENSE.i915=' <<<"$data_mapping"
grep -Fq '/usr/share/licenses/i915-firmware/WHENCE=' <<<"$data_mapping"
grep -Fq '/usr/share/zedbsd/packages/i915-firmware.manifest=' <<<"$data_mapping"
test "$(grep -o -- '/[^ =]*=' <<<"$data_mapping" | wc -l | tr -d '[:space:]')" = 5

echo 'i915 firmware package: PASS'
