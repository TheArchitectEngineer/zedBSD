#!/usr/bin/env bash
# The brcmbt-firmware package (ws215-p004): locked acquisition identity of
# both upstreams, default-off, selected by the Raspberry Pi 4 CI image only,
# hermetic fixture acquisition and cache verification, and the installed
# mapping (the patch file, the licence, the copyright record, the manifest).
# Host only; no network.  When a verified cache is at hand, the installed
# licence is checked against the copyright record's Cypress paragraph.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck source=/dev/null
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/brcmbt-firmware-test"
temporary=$fresh_dir

for command_name in awk cmp find git grep make mktemp python3 sha256sum wc; do
	command -v "$command_name" >/dev/null || {
		echo "missing host command: $command_name" >&2
		exit 1
	}
done

bluez_revision=adb4d55c8f193ff16ece74dd72fd6e8801632103
linux_revision=dc85ccedc9c973682fbcf4d628ca61174bcc3120
hcd_hash=51c45e77ddad91a19e96dc8fb75295b2087c279940df2634b23baf71b6dea42c
copyright_hash=79576e5ea6740845bf71ce96ad4e31cd49797c8e1a259d64670f48554b8eb67c
licence_hash=ae0db6cc4db33941148df0f67de53e76a77b1b5a46b3165edb7040aa2750015f
package_makefile=$repo/userland/firmware/brcmbt/Makefile
manifest=$repo/userland/firmware/brcmbt/brcmbt-firmware.manifest
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

if git -C "$repo" ls-files | grep -E '\.hcd$' >/dev/null; then
	echo 'Broadcom Bluetooth firmware bytes must not be tracked' >&2
	exit 1
fi

# The manifest names both immutable commits and every accepted byte.
grep -Fq "override BRCMBT_FIRMWARE_BLUEZ_REVISION := $bluez_revision" "$package_makefile"
grep -Fq "override BRCMBT_FIRMWARE_LINUX_REVISION := $linux_revision" "$package_makefile"
grep -Fq "override BRCMBT_FIRMWARE_LINUX_URL_SUFFIX := ?id=$linux_revision" "$package_makefile"
grep -Fq "acquisition-revision=$bluez_revision" "$manifest"
grep -Fq "acquisition-file=debian/firmware/broadcom/BCM4345C0.hcd size=63806 sha256=$hcd_hash" "$manifest"
grep -Fq "acquisition-copyright-sha256=$copyright_hash" "$manifest"
grep -Fq 'license-tag=20260410' "$manifest"
grep -Fq "license-revision=$linux_revision" "$manifest"
grep -Fq "license-sha256=$licence_hash" "$manifest"
grep -Fq 'redistribution=object-code-only, solely for use with Cypress integrated circuit products' "$manifest"
test "$(grep -c '^acquisition-file=' "$manifest")" = 1

# The manifest and the Makefile's locked list agree on every size and hash.
grep -Fq "BCM4345C0.hcd|bluez|debian/firmware/broadcom/BCM4345C0.hcd|63806|$hcd_hash" "$package_makefile"
grep -Fq "copyright|bluez|debian/copyright|$(sed -n 's/^acquisition-copyright-size=//p' "$manifest")|$copyright_hash" "$package_makefile"
grep -Fq "LICENCE.cypress|linux|LICENCE.cypress|$(sed -n 's/^license-size=//p' "$manifest")|$licence_hash" "$package_makefile"

# Production acquisition identity, accepted bytes, verifier, manifest, and
# rootfs mapping ignore command-line substitutions.
locked_metadata=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=rpi4 \
	ZEDBSD_ARCHITECTURE=arm64 ZEDBSD_BOARD=rpi4 ZEDBSD_VARIANT=default \
	BRCMBT_FIRMWARE_BLUEZ_BASE_URL=https://example.invalid/attacker \
	BRCMBT_FIRMWARE_BLUEZ_REVISION=attacker \
	BRCMBT_FIRMWARE_LINUX_BASE_URL=https://example.invalid/attacker \
	BRCMBT_FIRMWARE_LINUX_REVISION=attacker \
	BRCMBT_FIRMWARE_LINUX_URL_SUFFIX='?id=attacker' \
	BRCMBT_FIRMWARE_FILES='x|bluez|attacker|1|deadbeef' \
	BRCMBT_FIRMWARE_FETCH=false \
	BRCMBT_FIRMWARE_SHA256_COMMAND=false \
	BRCMBT_FIRMWARE_MANIFEST=/tmp/attacker-manifest \
	BRCMBT_FIRMWARE_DATA=/tmp/attacker-data \
	'USERLAND_brcmbt-firmware_DATA=/tmp/attacker-userland-data' \
	--eval='brcmbt-print-locked-metadata:;@printf "%s\n" "$(BRCMBT_FIRMWARE_BLUEZ_BASE_URL)|$(BRCMBT_FIRMWARE_BLUEZ_REVISION)|$(BRCMBT_FIRMWARE_LINUX_BASE_URL)|$(BRCMBT_FIRMWARE_LINUX_REVISION)|$(BRCMBT_FIRMWARE_LINUX_URL_SUFFIX)|$(BRCMBT_FIRMWARE_FETCH)|$(BRCMBT_FIRMWARE_SHA256_COMMAND)|$(BRCMBT_FIRMWARE_MANIFEST)|$(strip $(BRCMBT_FIRMWARE_FILES))|$(BRCMBT_FIRMWARE_DATA)|$(USERLAND_brcmbt-firmware_DATA)"' \
	brcmbt-print-locked-metadata)
grep -Fq "https://raw.githubusercontent.com/RPi-Distro/bluez-firmware|$bluez_revision|https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain|$linux_revision|?id=$linux_revision|curl --fail --location --silent --show-error|sha256sum|userland/firmware/brcmbt/brcmbt-firmware.manifest|" <<<"$locked_metadata"
grep -Fq "BCM4345C0.hcd|bluez|debian/firmware/broadcom/BCM4345C0.hcd|63806|$hcd_hash" <<<"$locked_metadata"
grep -Fq '/lib/firmware/brcm/BCM4345C0.hcd=' <<<"$locked_metadata"
if grep -Eq 'attacker|deadbeef|\|false\|' <<<"$locked_metadata"; then
	echo 'production brcmbt metadata accepted a command-line substitution' >&2
	exit 1
fi

# Package discovery and the default configuration cannot select or acquire
# it; of the checked-in image configurations only the Raspberry Pi 4 one
# selects it (the licence's Purpose: Cypress integrated circuits).
default_cache=$temporary/default-cache
default_config=$temporary/default.mk
make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	BRCMBT_FIRMWARE_CACHE_ROOT="$default_cache" \
	list-user-programs >"$temporary/programs"
grep -Fq 'brcmbt-firmware|Broadcom UART Bluetooth firmware|*|n|firmware|firmware/brcmbt|' "$temporary/programs"
ZEDBSD_CONFIG="$absent_config" \
	python3 "$repo/tools/menuconfig.py" --defaults --output "$default_config"
if grep '^ZEDBSD_USER_PROGRAMS' "$default_config" | grep -Fq 'brcmbt-firmware'; then
	echo 'brcmbt firmware must be default-off' >&2
	exit 1
fi
test ! -e "$default_cache"
default_data=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$default_config" \
	--eval='brcmbt-print-default-data:;@printf "%s\n" "$(ZEDBSD_USERLAND_DATA_INPUTS)"' \
	brcmbt-print-default-data)
if grep -Fq 'BCM4345C0' <<<"$default_data"; then
	echo 'ordinary default image depends on brcmbt firmware' >&2
	exit 1
fi
test ! -e "$default_cache"
selecting=$(grep -l '^ZEDBSD_USER_PROGRAMS.*[ =]brcmbt-firmware\( \|$\)' "$repo"/config/*/*.mk "$repo"/config/*.mk 2>/dev/null || true)
test "$selecting" = "$repo/config/ci/config-rpi4.mk" || {
	echo "brcmbt firmware selected by an unexpected configuration: $selecting" >&2
	exit 1
}

# The fixture override exists only for its single hermetic goal.
if make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	brcmbt-firmware-fixture-cache list-user-programs \
	>"$temporary/mixed-goal.log" 2>&1; then
	echo 'fixture goal unexpectedly accepted a second goal' >&2
	exit 1
fi
grep -Fq 'must be the only requested goal' "$temporary/mixed-goal.log"

bluez_fixture_revision=brcmbt-fixture-bluez
linux_fixture_revision=brcmbt-fixture-linux
fixture=$temporary/mirror
mkdir -p "$fixture/bluez/$bluez_fixture_revision/debian/firmware/broadcom" "$fixture/linux"
printf 'brcmbt fixture hcd\n' >"$fixture/bluez/$bluez_fixture_revision/debian/firmware/broadcom/BCM4345C0.hcd"
printf 'brcmbt fixture copyright\n' >"$fixture/bluez/$bluez_fixture_revision/debian/copyright"
printf 'brcmbt fixture licence\n' >"$fixture/linux/LICENCE.cypress"
fixture_entry() {
	local name=$1 source=$2 path=$3 file=$4
	printf '%s|%s|%s|%s|%s' "$name" "$source" "$path" \
		"$(wc -c <"$file" | tr -d '[:space:]')" \
		"$(sha256sum "$file" | awk '{print $1}')"
}
fixture_files="$(fixture_entry BCM4345C0.hcd bluez debian/firmware/broadcom/BCM4345C0.hcd "$fixture/bluez/$bluez_fixture_revision/debian/firmware/broadcom/BCM4345C0.hcd") $(fixture_entry copyright bluez debian/copyright "$fixture/bluez/$bluez_fixture_revision/debian/copyright") $(fixture_entry LICENCE.cypress linux LICENCE.cypress "$fixture/linux/LICENCE.cypress")"
names='BCM4345C0.hcd copyright LICENCE.cypress'

run_package() {
	local cache_root=$1
	local fetch_command=$2
	local bluez_base=${3:-file://$fixture/bluez}
	FETCH_LOG=$fetch_log make -C "$repo" --no-print-directory \
		ZEDBSD_CONFIG="$absent_config" \
		BRCMBT_FIRMWARE_BLUEZ_BASE_URL="$bluez_base" \
		BRCMBT_FIRMWARE_BLUEZ_REVISION="$bluez_fixture_revision" \
		BRCMBT_FIRMWARE_LINUX_BASE_URL="file://$fixture/linux" \
		BRCMBT_FIRMWARE_LINUX_REVISION="$linux_fixture_revision" \
		BRCMBT_FIRMWARE_LINUX_URL_SUFFIX= \
		BRCMBT_FIRMWARE_FILES="$fixture_files" \
		BRCMBT_FIRMWARE_CACHE_ROOT="$cache_root" \
		BRCMBT_FIRMWARE_FETCH="$fetch_command" \
		brcmbt-firmware-fixture-cache
}

cache_root=$temporary/cache
: >"$fetch_log"
run_package "$cache_root" "$fetcher"
cache=$cache_root/$bluez_fixture_revision-$linux_fixture_revision
test "$(wc -l <"$fetch_log" | tr -d '[:space:]')" = 3
grep -Fq "file://$fixture/bluez/$bluez_fixture_revision/debian/firmware/broadcom/BCM4345C0.hcd" "$fetch_log"
grep -Fq "file://$fixture/linux/LICENCE.cypress" "$fetch_log"
cmp -s "$cache/BCM4345C0.hcd" "$fixture/bluez/$bluez_fixture_revision/debian/firmware/broadcom/BCM4345C0.hcd"
cmp -s "$cache/copyright" "$fixture/bluez/$bluez_fixture_revision/debian/copyright"
cmp -s "$cache/LICENCE.cypress" "$fixture/linux/LICENCE.cypress"

# A complete cache is reusable offline.  A missing cache, unsafe path,
# unexpected file, or any corrupt object fails without replacing state.
: >"$fetch_log"
run_package "$cache_root" "$fail_fetcher"
test ! -s "$fetch_log"

missing_root=$temporary/missing
if run_package "$missing_root" "$fail_fetcher" >"$temporary/missing.log" 2>&1; then
	echo 'missing offline brcmbt cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unexpected firmware network fetch' "$temporary/missing.log"
test ! -e "$missing_root/$bluez_fixture_revision-$linux_fixture_revision"

snapshot=$temporary/snapshot
cp -a "$cache" "$snapshot"
cache_name=$bluez_fixture_revision-$linux_fixture_revision
for corrupt_name in $names; do
	corrupt_root=$temporary/corrupt-$corrupt_name
	mkdir -p "$corrupt_root"
	cp -a "$snapshot" "$corrupt_root/$cache_name"
	printf 'corrupt\n' >>"$corrupt_root/$cache_name/$corrupt_name"
	if run_package "$corrupt_root" "$fail_fetcher" >"$temporary/corrupt.log" 2>&1; then
		echo "corrupt brcmbt cache unexpectedly accepted: $corrupt_name" >&2
		exit 1
	fi
	grep -Eq 'size mismatch|SHA-256 mismatch' "$temporary/corrupt.log"
done

# Same size, different bytes: only the hash can catch it.
flip_root=$temporary/flip
mkdir -p "$flip_root"
cp -a "$snapshot" "$flip_root/$cache_name"
printf 'brcmbt fixture hcX\n' >"$flip_root/$cache_name/BCM4345C0.hcd"
if run_package "$flip_root" "$fail_fetcher" >"$temporary/flip.log" 2>&1; then
	echo 'same-size corrupt brcmbt cache unexpectedly accepted' >&2
	exit 1
fi
grep -Fq 'SHA-256 mismatch' "$temporary/flip.log"

extra_root=$temporary/extra
mkdir -p "$extra_root"
cp -a "$snapshot" "$extra_root/$cache_name"
: >"$extra_root/$cache_name/unexpected"
if run_package "$extra_root" "$fail_fetcher" >"$temporary/extra.log" 2>&1; then
	echo 'brcmbt cache with an extra object unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'cache must contain exactly 3 files' "$temporary/extra.log"

link_root=$temporary/link-file
mkdir -p "$link_root/$cache_name"
cp -a "$snapshot/." "$link_root/$cache_name/"
mv "$link_root/$cache_name/LICENCE.cypress" "$link_root/LICENCE.real"
ln -s ../LICENCE.real "$link_root/$cache_name/LICENCE.cypress"
if run_package "$link_root" "$fail_fetcher" >"$temporary/link-file.log" 2>&1; then
	echo 'brcmbt cache with a symlinked file unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'missing or unsafe cache file' "$temporary/link-file.log"

unsafe_root=$temporary/unsafe
mkdir -p "$unsafe_root"
ln -s "$snapshot" "$unsafe_root/$cache_name"
if run_package "$unsafe_root" "$fail_fetcher" >"$temporary/unsafe.log" 2>&1; then
	echo 'symlink brcmbt cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unsafe cache path' "$temporary/unsafe.log"

# A partial fetch (the bluez tree reachable, the licence not) publishes
# nothing.
partial_fixture=$temporary/partial-mirror
partial_root=$temporary/partial-cache
mkdir -p "$partial_fixture/$bluez_fixture_revision"
cp -a "$fixture/bluez/$bluez_fixture_revision/." "$partial_fixture/$bluez_fixture_revision/"
if FETCH_LOG=$fetch_log make -C "$repo" --no-print-directory \
	ZEDBSD_CONFIG="$absent_config" \
	BRCMBT_FIRMWARE_BLUEZ_BASE_URL="file://$partial_fixture" \
	BRCMBT_FIRMWARE_BLUEZ_REVISION="$bluez_fixture_revision" \
	BRCMBT_FIRMWARE_LINUX_BASE_URL="file://$temporary/no-such-mirror" \
	BRCMBT_FIRMWARE_LINUX_REVISION="$linux_fixture_revision" \
	BRCMBT_FIRMWARE_LINUX_URL_SUFFIX= \
	BRCMBT_FIRMWARE_FILES="$fixture_files" \
	BRCMBT_FIRMWARE_CACHE_ROOT="$partial_root" \
	BRCMBT_FIRMWARE_FETCH="$fetcher" \
	brcmbt-firmware-fixture-cache >"$temporary/partial.log" 2>&1; then
	echo 'partial brcmbt acquisition unexpectedly succeeded' >&2
	exit 1
fi
test ! -e "$partial_root/$cache_name"
test -z "$(find "$partial_root" -mindepth 1 -print -quit)"

# The selected rootfs mapping: the patch file, the licence, the copyright
# record, and the manifest.
data_mapping=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=rpi4 \
	ZEDBSD_ARCHITECTURE=arm64 ZEDBSD_BOARD=rpi4 ZEDBSD_VARIANT=default \
	ZEDBSD_USER_PROGRAMS=brcmbt-firmware \
	--eval='brcmbt-print-firmware-data:;@printf "%s\n" "$(BRCMBT_FIRMWARE_DATA)"' \
	brcmbt-print-firmware-data)
test "$(grep -o -- '/[^ =]*=' <<<"$data_mapping" | wc -l | tr -d '[:space:]')" = 4
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/lib/firmware/brcm/BCM4345C0\.hcd=.*/BCM4345C0\.hcd$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/usr/share/licenses/brcmbt-firmware/LICENCE\.cypress=.*/LICENCE\.cypress$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/usr/share/licenses/brcmbt-firmware/bluez-firmware\.copyright=.*/copyright$'
grep -Fq '/usr/share/zedbsd/packages/brcmbt-firmware.manifest=userland/firmware/brcmbt/brcmbt-firmware.manifest' <<<"$data_mapping"

# When this package's verified cache is at hand, the installed plain
# licence equals the copyright record's Cypress licence paragraph (DEP-5
# indentation and its trailing separator removed), and the patch file is a
# sequence of HCI vendor commands ending in Launch_RAM.
real_cache=$repo/build/sources/firmware/brcmbt/$bluez_revision-$linux_revision
if test -d "$real_cache" && test ! -L "$real_cache"; then
	test "$(sha256sum "$real_cache/BCM4345C0.hcd" | awk '{print $1}')" = "$hcd_hash"
	test "$(sha256sum "$real_cache/copyright" | awk '{print $1}')" = "$copyright_hash"
	test "$(sha256sum "$real_cache/LICENCE.cypress" | awk '{print $1}')" = "$licence_hash"
	grep -Fqx 'Files: debian/firmware/broadcom/*' "$real_cache/copyright"
	awk '
		/^License: CYPRESS-WIRELESS-CONNECTIVITY-DEVICES-DRIVER-END-USER-LICENSE-AGREEMENT$/ { if (seen++) on = 1; next }
		on && /^[^ ]/ { exit }
		on { sub(/^ /, ""); if ($0 == ".") $0 = ""; print }
	' "$real_cache/copyright" >"$temporary/dep5-cypress.txt"
	sed '${/^$/d;}' "$temporary/dep5-cypress.txt" >"$temporary/dep5-cypress.trimmed"
	cmp -s "$temporary/dep5-cypress.trimmed" "$real_cache/LICENCE.cypress" || {
		echo 'LICENCE.cypress differs from the copyright record Cypress paragraph' >&2
		exit 1
	}
	python3 -I - "$real_cache/BCM4345C0.hcd" <<'EOF'
import sys

data = open(sys.argv[1], "rb").read()
offset = 0
opcodes = []
while offset < len(data):
	if offset + 3 > len(data):
		sys.exit("truncated record header")
	opcode = data[offset] | (data[offset + 1] << 8)
	length = data[offset + 2]
	offset += 3 + length
	if offset > len(data):
		sys.exit("truncated record")
	if opcode >> 10 != 0x3F:
		sys.exit("non-vendor opcode 0x%04x" % opcode)
	opcodes.append(opcode)
if opcodes[-1] != 0xFC4E or opcodes.count(0xFC4E) != 1:
	sys.exit("file does not end in one Launch_RAM")
if opcodes.count(0xFC4C) != len(opcodes) - 1:
	sys.exit("records other than Write_RAM before Launch_RAM")
print("brcmbt firmware package: %d Write_RAM records and Launch_RAM" % (len(opcodes) - 1))
EOF
	echo 'brcmbt firmware package: licence and copyright record match'
else
	echo 'brcmbt firmware package: licence cross-check skipped (no verified cache)'
fi

echo 'brcmbt firmware package: PASS'
