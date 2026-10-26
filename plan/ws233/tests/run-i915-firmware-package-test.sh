#!/usr/bin/env bash
# The i915-firmware package (userland/firmware/i915): its locked metadata, its
# default-off selection, the hermetic fixture acquisition of the six DMC
# programs the driver names with LICENSE.i915 and WHENCE, the offline reuse
# and the refusal of a missing, corrupt, extended, symlinked or partial cache,
# and the rootfs mapping.
#
# The run's files are in a new directory under build/tmp/ws233-i915-firmware
# (plan/tools/fresh-out.sh); nothing is removed here, Q1's
# plan/tools/q1-clean.sh removes the runs the name does not point at.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws233-i915-firmware"
temporary=$fresh_dir

for command_name in awk cmp curl find git grep make mktemp python3 sha256sum wc; do
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

# The DMC programs: the Makefile's variable stem, the file, and its accepted
# size and SHA-256 at linux-firmware dc85cced (tag 20260410).
dmc_programs='ADLP_DMC|adlp_dmc.bin|79088|3516de2e134ddcf3b319c75d2e437779fecbd58cbb77234bd6f297c544e92ccb
TGL_DMC|tgl_dmc_ver2_12.bin|19760|3c013ef0ad96ba73aee8e5bd04a8e27cc9b1c6e9183b1a83ce124485f325afca
ADLP_FALLBACK_DMC|adlp_dmc_ver2_16.bin|77084|2da482ea46a40e54c9ca3b54185959177f393eff98ece21acdac7eb6cacb0fcb
ADLS_DMC|adls_dmc_ver2_01.bin|18704|96e426ed7e80f0cc6644ada1928f4d02b163ff59b14b2339aa28a374d65a5b19
ICL_DMC|icl_dmc_ver1_09.bin|25952|aafad1967679baa36e971e5331da73eea31c68239416432df83e71332755dc01
RKL_DMC|rkl_dmc_ver2_03.bin|18476|139ce0eb873a13679a4685463cdacf6d247073ce286c8a5bca443ef1d17ee2ac'
dmc_files=$(printf '%s\n' "$dmc_programs" | awk -F'|' '{print $2}')
dmc_count=$(printf '%s\n' "$dmc_programs" | wc -l | tr -d '[:space:]')
# The files of a complete cache: the DMC programs, LICENSE.i915 and WHENCE.
cache_count=$((dmc_count + 2))

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

if git -C "$repo" ls-files | grep -E '(^|/)[a-z]+_dmc[a-z0-9_]*\.bin$' >/dev/null; then
	echo 'i915 DMC firmware bytes must not be tracked' >&2
	exit 1
fi

# Static package metadata and the manifest describe the exact DMC bytes of the
# official immutable linux-firmware commit and the complete LICENSE.i915 and
# WHENCE.
grep -Fq 'override I915_FIRMWARE_REVISION := dc85ccedc9c973682fbcf4d628ca61174bcc3120' "$package_makefile"
grep -Fq 'override I915_FIRMWARE_URL_SUFFIX := ?id=dc85ccedc9c973682fbcf4d628ca61174bcc3120' "$package_makefile"
grep -Fq 'override I915_FIRMWARE_CACHE_SET := all-dmc' "$package_makefile"
grep -Fq 'acquisition-tag=20260410' "$manifest"
grep -Fq 'acquisition-tag-object=4585dd5a5f0cee08990d754701d8866d9e9266e6' "$manifest"
grep -Fq 'acquisition-revision=dc85ccedc9c973682fbcf4d628ca61174bcc3120' "$manifest"
while IFS='|' read -r stem file size hash; do
	key=$(printf '%s' "$stem" | tr 'A-Z_' 'a-z-')
	grep -Fq "override I915_FIRMWARE_${stem}_SIZE := $size" "$package_makefile"
	grep -Fq "override I915_FIRMWARE_${stem}_SHA256 := $hash" "$package_makefile"
	grep -Fq "installed-$key=/lib/firmware/i915/$file" "$manifest"
	grep -Fq "acquisition-$key-path=i915/$file" "$manifest"
	grep -Fq "acquisition-$key-size=$size" "$manifest"
	grep -Fq "acquisition-$key-sha256=$hash" "$manifest"
done <<<"$dmc_programs"
grep -Fq 'acquisition-license-path=LICENSE.i915' "$manifest"
grep -Fq 'acquisition-license-size=2080' "$manifest"
grep -Fq 'acquisition-license-sha256=8542aeabf2761935122d693561e16766ce1bcc2b0d003204f9040b7d6d929f2e' "$manifest"
grep -Fq 'acquisition-whence-size=425450' "$manifest"
grep -Fq 'acquisition-whence-sha256=c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527' "$manifest"
grep -Fq 'adlp-dmc-version=2.20' "$manifest"
grep -Fq 'tgl-dmc-version=2.12' "$manifest"
grep -Fq 'redistribution=unmodified-binary-only' "$manifest"

# The driver names no DMC program the package does not install.
named=$(grep -rhoE '"i915/[a-z0-9_]+_dmc[a-z0-9_]*\.bin"' "$repo/src/drivers/gpu/i915" \
	--include='*.c' --include='*.h' --include='*.inc' | tr -d '"' | sed 's|^i915/||' | sort -u)
for file in $named; do
	if ! grep -qx "$file" <<<"$dmc_files"; then
		echo "the driver names $file, which the package does not install" >&2
		exit 1
	fi
done

# The kernel carries no DMC bytes (LICENSE.i915 firmware stays out of it):
# the driver reads /lib/firmware/i915/*.bin, which this package installs.
if grep -rlE 'drv_i915_firmware_(adlp|tgl)_dmc' "$repo/src/drivers/gpu/i915" \
	--include='*.c' --include='*.h' --include='*.inc' >/dev/null; then
	echo 'i915 DMC firmware bytes must not be embedded in the kernel' >&2
	exit 1
fi

# Production acquisition identity, accepted bytes, verifier, manifest, and
# rootfs mapping must ignore command-line substitutions.
substitutions=()
printed='$(I915_FIRMWARE_BASE_URL)|$(I915_FIRMWARE_REVISION)|$(I915_FIRMWARE_URL_SUFFIX)'
expected='https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain|dc85ccedc9c973682fbcf4d628ca61174bcc3120|?id=dc85ccedc9c973682fbcf4d628ca61174bcc3120'
while IFS='|' read -r stem file size hash; do
	substitutions+=("I915_FIRMWARE_${stem}_SIZE=1" "I915_FIRMWARE_${stem}_SHA256=deadbeef")
	printed="$printed|\$(I915_FIRMWARE_${stem}_SIZE)|\$(I915_FIRMWARE_${stem}_SHA256)"
	expected="$expected|$size|$hash"
done <<<"$dmc_programs"
printed="$printed"'|$(I915_FIRMWARE_LICENSE_SIZE)|$(I915_FIRMWARE_LICENSE_SHA256)|$(I915_FIRMWARE_WHENCE_SIZE)|$(I915_FIRMWARE_WHENCE_SHA256)|$(I915_FIRMWARE_FETCH)|$(I915_FIRMWARE_SHA256_COMMAND)|$(I915_FIRMWARE_MANIFEST)|$(I915_FIRMWARE_CACHE_SET)|$(I915_FIRMWARE_DATA)|$(USERLAND_i915-firmware_DATA)'
expected="$expected"'|2080|8542aeabf2761935122d693561e16766ce1bcc2b0d003204f9040b7d6d929f2e|425450|c282239a5a2d849677e9304e6f361e475e1b6e71e7c771c03f8986f71b309527|curl --fail --location --silent --show-error --retry 3 --retry-all-errors --retry-delay 5|sha256sum|userland/firmware/i915/i915-firmware.manifest|all-dmc|'
locked_metadata=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=amd64 \
	ZEDBSD_ARCHITECTURE=amd64 ZEDBSD_BOARD=pcat ZEDBSD_VARIANT=uefi \
	I915_FIRMWARE_BASE_URL=https://example.invalid/attacker \
	I915_FIRMWARE_REVISION=attacker \
	I915_FIRMWARE_URL_SUFFIX='?id=attacker' \
	"${substitutions[@]}" \
	I915_FIRMWARE_LICENSE_SIZE=1 \
	I915_FIRMWARE_LICENSE_SHA256=deadbeef \
	I915_FIRMWARE_WHENCE_SIZE=1 \
	I915_FIRMWARE_WHENCE_SHA256=deadbeef \
	I915_FIRMWARE_FETCH=false \
	I915_FIRMWARE_SHA256_COMMAND=false \
	I915_FIRMWARE_MANIFEST=/tmp/attacker-manifest \
	I915_FIRMWARE_CACHE_SET=attacker \
	I915_FIRMWARE_DATA=/tmp/attacker-data \
	'USERLAND_i915-firmware_DATA=/tmp/attacker-userland-data' \
	--eval="i915fw-print-locked-metadata:;@printf '%s\n' \"$printed\"" \
	i915fw-print-locked-metadata)
grep -Fq "$expected" <<<"$locked_metadata"
while IFS='|' read -r stem file size hash; do
	grep -Fq "/lib/firmware/i915/$file=" <<<"$locked_metadata"
done <<<"$dmc_programs"
if grep -Eq 'attacker|deadbeef|/tmp/attacker|\|false\|' <<<"$locked_metadata"; then
	echo 'production i915 metadata accepted a command-line substitution' >&2
	exit 1
fi

# Package discovery and default configuration are metadata-only and acquire
# nothing.  The package itself is default-off; the default configuration
# selects it because the i915 driver is on (config/drivers/firmware.list,
# 2026-10-14 user, WS232 p004).
default_cache=$temporary/default-cache
default_config=$temporary/default.mk
make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	I915_FIRMWARE_CACHE_ROOT="$default_cache" \
	list-user-programs >"$temporary/programs"
# The platform column is the listing's (amd64, or * for every platform); the package is default-off.
grep -Eq '^i915-firmware\|Intel i915 display DMC firmware\|[^|]+\|n\|firmware\|firmware/i915\|' "$temporary/programs"
ZEDBSD_CONFIG="$absent_config" \
	python3 "$repo/tools/menuconfig.py" --defaults --output "$default_config"
grep '^CONFIG_DRIVER_PCI_I915 *:\?= *y' "$default_config" >/dev/null
if ! grep '^ZEDBSD_USER_PROGRAMS' "$default_config" | \
	grep -Fq 'i915-firmware'; then
	echo 'the default configuration with the i915 driver must select i915 firmware' >&2
	exit 1
fi
test ! -e "$default_cache"
default_data=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$default_config" \
	--eval='i915fw-print-default-data:;@printf "%s\n" "$(ZEDBSD_USERLAND_DATA_INPUTS)"' \
	i915fw-print-default-data)
for file in $dmc_files; do
	grep -Fq -- "-all-dmc/$file" <<<"$default_data"
done
test ! -e "$default_cache"

# The fixture override exists only for its single hermetic goal.
if make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	i915-firmware-fixture-cache list-user-programs \
	>"$temporary/mixed-goal.log" 2>&1; then
	echo 'fixture goal unexpectedly accepted a second goal' >&2
	exit 1
fi
grep -Fq 'must be the only requested goal' "$temporary/mixed-goal.log"

# A fixture mirror: one small distinct file for each object.
revision=i915fw-fixture-revision
cache_name=$revision-all-dmc
fixture=$temporary/mirror
mkdir -p "$fixture/i915"
for file in $dmc_files; do
	printf 'i915fw %s fixture\n' "$file" >"$fixture/i915/$file"
done
printf 'i915fw Intel firmware license fixture\n' >"$fixture/LICENSE.i915"
printf 'i915fw WHENCE fixture\n' >"$fixture/WHENCE"

# The fixture's sizes and hashes, as the fixture goal's command-line values.
fixture_values=()
while IFS='|' read -r stem file size hash; do
	fixture_values+=(
		"I915_FIRMWARE_${stem}_SIZE=$(wc -c <"$fixture/i915/$file" | tr -d '[:space:]')"
		"I915_FIRMWARE_${stem}_SHA256=$(sha256sum "$fixture/i915/$file" | awk '{print $1}')")
done <<<"$dmc_programs"
license_size=$(wc -c <"$fixture/LICENSE.i915" | tr -d '[:space:]')
whence_size=$(wc -c <"$fixture/WHENCE" | tr -d '[:space:]')
license_hash=$(sha256sum "$fixture/LICENSE.i915" | awk '{print $1}')
whence_hash=$(sha256sum "$fixture/WHENCE" | awk '{print $1}')

# run_package MIRROR CACHE-ROOT FETCH: the fixture goal against MIRROR.
run_package() {
	FETCH_LOG=$fetch_log make -C "$repo" --no-print-directory \
		ZEDBSD_CONFIG="$absent_config" \
		I915_FIRMWARE_BASE_URL="file://$1" \
		I915_FIRMWARE_REVISION="$revision" \
		I915_FIRMWARE_URL_SUFFIX= \
		"${fixture_values[@]}" \
		I915_FIRMWARE_LICENSE_SIZE="$license_size" \
		I915_FIRMWARE_LICENSE_SHA256="$license_hash" \
		I915_FIRMWARE_WHENCE_SIZE="$whence_size" \
		I915_FIRMWARE_WHENCE_SHA256="$whence_hash" \
		I915_FIRMWARE_CACHE_ROOT="$2" \
		I915_FIRMWARE_FETCH="$3" \
		i915-firmware-fixture-cache
}

cache_root=$temporary/cache
: >"$fetch_log"
run_package "$fixture" "$cache_root" "$fetcher"
cache=$cache_root/$cache_name
test "$(wc -l <"$fetch_log" | tr -d '[:space:]')" = "$cache_count"
for file in $dmc_files; do
	cmp -s "$cache/$file" "$fixture/i915/$file"
done
cmp -s "$cache/LICENSE.i915" "$fixture/LICENSE.i915"
cmp -s "$cache/WHENCE" "$fixture/WHENCE"

# A complete cache is reusable offline.  A missing cache, unsafe path,
# unexpected file, or any independently corrupt object must fail without
# silently replacing existing state.
: >"$fetch_log"
run_package "$fixture" "$cache_root" "$fail_fetcher"
test ! -s "$fetch_log"

missing_root=$temporary/missing
if run_package "$fixture" "$missing_root" "$fail_fetcher" \
	>"$temporary/missing.log" 2>&1; then
	echo 'missing offline i915 cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unexpected firmware network fetch' "$temporary/missing.log"
test ! -e "$missing_root/$cache_name"

snapshot=$temporary/snapshot
cp -a "$cache" "$snapshot"
for corrupt_name in $dmc_files LICENSE.i915 WHENCE; do
	corrupt_root=$temporary/corrupt-$corrupt_name
	mkdir -p "$corrupt_root"
	cp -a "$snapshot" "$corrupt_root/$cache_name"
	printf 'corrupt\n' >>"$corrupt_root/$cache_name/$corrupt_name"
	if run_package "$fixture" "$corrupt_root" "$fail_fetcher" \
		>"$temporary/corrupt.log" 2>&1; then
		echo "corrupt i915 cache unexpectedly accepted: $corrupt_name" >&2
		exit 1
	fi
	grep -Eq 'size mismatch|SHA-256 mismatch' "$temporary/corrupt.log"
done

extra_root=$temporary/extra
mkdir -p "$extra_root"
cp -a "$snapshot" "$extra_root/$cache_name"
: >"$extra_root/$cache_name/unexpected"
if run_package "$fixture" "$extra_root" "$fail_fetcher" \
	>"$temporary/extra.log" 2>&1; then
	echo 'i915 cache with an extra object unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'cache must contain exactly eight files' "$temporary/extra.log"

unsafe_root=$temporary/unsafe
mkdir -p "$unsafe_root"
ln -s "$snapshot" "$unsafe_root/$cache_name"
if run_package "$fixture" "$unsafe_root" "$fail_fetcher" \
	>"$temporary/unsafe.log" 2>&1; then
	echo 'symlink i915 cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unsafe cache path' "$temporary/unsafe.log"

# A partial fetch (a mirror with one program only) publishes no cache; the
# package's own recipe removes its temporary directory.
partial_fixture=$temporary/partial-mirror
partial_root=$temporary/partial-cache
mkdir -p "$partial_fixture/i915"
cp "$fixture/i915/adlp_dmc.bin" "$partial_fixture/i915/"
if run_package "$partial_fixture" "$partial_root" "$fetcher" \
	>"$temporary/partial.log" 2>&1; then
	echo 'partial i915 acquisition unexpectedly succeeded' >&2
	exit 1
fi
test ! -e "$partial_root/$cache_name"
test -z "$(find "$partial_root" -mindepth 1 -print -quit)"

# The selected rootfs mapping installs the DMC programs plus the full Intel
# license, full WHENCE, and static provenance manifest.
data_mapping=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=amd64 \
	ZEDBSD_ARCHITECTURE=amd64 ZEDBSD_BOARD=pcat ZEDBSD_VARIANT=uefi \
	ZEDBSD_USER_PROGRAMS=i915-firmware \
	--eval='i915fw-print-firmware-data:;@printf "%s\n" "$(I915_FIRMWARE_DATA)"' \
	i915fw-print-firmware-data)
for file in $dmc_files; do
	grep -Fq "/lib/firmware/i915/$file=" <<<"$data_mapping"
done
grep -Fq '/usr/share/licenses/i915-firmware/LICENSE.i915=' <<<"$data_mapping"
grep -Fq '/usr/share/licenses/i915-firmware/WHENCE=' <<<"$data_mapping"
grep -Fq '/usr/share/zedbsd/packages/i915-firmware.manifest=' <<<"$data_mapping"
test "$(grep -o -- '/[^ =]*=' <<<"$data_mapping" | wc -l | tr -d '[:space:]')" = $((dmc_count + 3))

echo 'i915 firmware package: PASS'
