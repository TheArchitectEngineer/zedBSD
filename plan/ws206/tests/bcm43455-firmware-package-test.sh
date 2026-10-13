#!/usr/bin/env bash
# The bcm43455-firmware package (ws206-p003): locked acquisition identity of
# both upstreams, default-off, selected by the Raspberry Pi 4 CI image only,
# hermetic fixture acquisition and cache verification, and the installed
# mapping (firmware, CLM, NVRAM, two licences, the copyright record, the
# manifest).  Host only; no network.  When a verified cache is at hand, the
# installed Cypress licence is checked against the copyright record's
# paragraph and the three chip files against the shapes the driver checks.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck source=/dev/null
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/bcm43455-firmware-test"
temporary=$fresh_dir

for command_name in awk cmp find git grep make mktemp python3 sha256sum wc; do
	command -v "$command_name" >/dev/null || {
		echo "missing host command: $command_name" >&2
		exit 1
	}
done

nonfree_revision=3bab0f823f5b53150b76aab77093adef6655b920
linux_revision=dc85ccedc9c973682fbcf4d628ca61174bcc3120
firmware_hash=3075cb0bdc4b28ed4f08e01b1a216d0ebc70f4022d9d3272a4a43b3c90456e60
clm_hash=9823842cae9fb9a5dd1e5fb31f595516ec7deee341354bef30bb3026eee29cc1
nvram_hash=ca709be81a78bdb6932936374f39943acbd7af07fae6151011127599a3ce9e3d
copyright_hash=07082fd0bb65e32c73af39a2292dd6c83f169be7427d338ba1fe34f9b86c0e30
licence_hash=ae0db6cc4db33941148df0f67de53e76a77b1b5a46b3165edb7040aa2750015f
gpl_hash=8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643
package_makefile=$repo/userland/firmware/bcm43455/Makefile
manifest=$repo/userland/firmware/bcm43455/bcm43455-firmware.manifest
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

if git -C "$repo" ls-files | grep -E '43455.*\.(bin|clm_blob)$|brcmfmac43455.*\.txt$' >/dev/null; then
	echo 'BCM43455 firmware bytes must not be tracked' >&2
	exit 1
fi

# The manifest names both immutable commits and every accepted byte.
grep -Fq "override BCM43455_FIRMWARE_NONFREE_REVISION := $nonfree_revision" "$package_makefile"
grep -Fq "override BCM43455_FIRMWARE_LINUX_REVISION := $linux_revision" "$package_makefile"
grep -Fq "override BCM43455_FIRMWARE_LINUX_URL_SUFFIX := ?id=$linux_revision" "$package_makefile"
grep -Fq "acquisition-revision=$nonfree_revision" "$manifest"
grep -Fq "acquisition-file=debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin size=548728 sha256=$firmware_hash " "$manifest"
grep -Fq "acquisition-file=debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob size=2676 sha256=$clm_hash " "$manifest"
grep -Fq "acquisition-file=debian/added-firmware/brcm/brcmfmac43455-sdio.txt size=2074 sha256=$nvram_hash " "$manifest"
grep -Fq "acquisition-copyright-sha256=$copyright_hash" "$manifest"
grep -Fq 'license-tag=20260410' "$manifest"
grep -Fq "license-revision=$linux_revision" "$manifest"
grep -Fq "license-sha256=$licence_hash" "$manifest"
grep -Fq "gpl-sha256=$gpl_hash" "$manifest"
grep -Fq 'redistribution=object-code-only, solely for use with Cypress integrated circuit products' "$manifest"
grep -Fq 'regulatory=world:' "$manifest"
test "$(grep -c '^acquisition-file=' "$manifest")" = 3

# The manifest and the Makefile's locked list agree on every size and hash.
grep -Fq "cyfmac43455-sdio-minimal.bin|nonfree|debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin|548728|$firmware_hash" "$package_makefile"
grep -Fq "cyfmac43455-sdio.clm_blob|nonfree|debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob|2676|$clm_hash" "$package_makefile"
grep -Fq "brcmfmac43455-sdio.txt|nonfree|debian/added-firmware/brcm/brcmfmac43455-sdio.txt|2074|$nvram_hash" "$package_makefile"
grep -Fq "copyright|nonfree|debian/copyright|$(sed -n 's/^acquisition-copyright-size=//p' "$manifest")|$copyright_hash" "$package_makefile"
grep -Fq "LICENCE.cypress|linux|LICENCE.cypress|$(sed -n 's/^license-size=//p' "$manifest")|$licence_hash" "$package_makefile"
grep -Fq "GPL-2|linux|GPL-2|$(sed -n 's/^gpl-size=//p' "$manifest")|$gpl_hash" "$package_makefile"

# Production acquisition identity, accepted bytes, verifier, manifest, and
# rootfs mapping ignore command-line substitutions.
locked_metadata=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=rpi4 \
	ZEDBSD_ARCHITECTURE=arm64 ZEDBSD_BOARD=rpi4 ZEDBSD_VARIANT=default \
	BCM43455_FIRMWARE_NONFREE_BASE_URL=https://example.invalid/attacker \
	BCM43455_FIRMWARE_NONFREE_REVISION=attacker \
	BCM43455_FIRMWARE_LINUX_BASE_URL=https://example.invalid/attacker \
	BCM43455_FIRMWARE_LINUX_REVISION=attacker \
	BCM43455_FIRMWARE_LINUX_URL_SUFFIX='?id=attacker' \
	BCM43455_FIRMWARE_FILES='x|nonfree|attacker|1|deadbeef' \
	BCM43455_FIRMWARE_FETCH=false \
	BCM43455_FIRMWARE_SHA256_COMMAND=false \
	BCM43455_FIRMWARE_MANIFEST=/tmp/attacker-manifest \
	BCM43455_FIRMWARE_DATA=/tmp/attacker-data \
	'USERLAND_bcm43455-firmware_DATA=/tmp/attacker-userland-data' \
	--eval='bcm43455-print-locked-metadata:;@printf "%s\n" "$(BCM43455_FIRMWARE_NONFREE_BASE_URL)|$(BCM43455_FIRMWARE_NONFREE_REVISION)|$(BCM43455_FIRMWARE_LINUX_BASE_URL)|$(BCM43455_FIRMWARE_LINUX_REVISION)|$(BCM43455_FIRMWARE_LINUX_URL_SUFFIX)|$(BCM43455_FIRMWARE_FETCH)|$(BCM43455_FIRMWARE_SHA256_COMMAND)|$(BCM43455_FIRMWARE_MANIFEST)|$(strip $(BCM43455_FIRMWARE_FILES))|$(BCM43455_FIRMWARE_DATA)|$(USERLAND_bcm43455-firmware_DATA)"' \
	bcm43455-print-locked-metadata)
grep -Fq "https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree|$nonfree_revision|https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain|$linux_revision|?id=$linux_revision|curl --fail --location --silent --show-error|sha256sum|userland/firmware/bcm43455/bcm43455-firmware.manifest|" <<<"$locked_metadata"
grep -Fq "cyfmac43455-sdio-minimal.bin|nonfree|debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin|548728|$firmware_hash" <<<"$locked_metadata"
grep -Fq '/lib/firmware/cypress/cyfmac43455-sdio-minimal.bin=' <<<"$locked_metadata"
if grep -Eq 'attacker|deadbeef|\|false\|' <<<"$locked_metadata"; then
	echo 'production bcm43455 metadata accepted a command-line substitution' >&2
	exit 1
fi

# Package discovery and the default configuration cannot select or acquire
# it; of the checked-in image configurations only the Raspberry Pi 4 one
# selects it (the licence's Purpose: Cypress integrated circuits).
default_cache=$temporary/default-cache
default_config=$temporary/default.mk
make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	BCM43455_FIRMWARE_CACHE_ROOT="$default_cache" \
	list-user-programs >"$temporary/programs"
grep -Fq 'bcm43455-firmware|Broadcom BCM43455 SDIO WiFi firmware|*|n|firmware|firmware/bcm43455|' "$temporary/programs"
ZEDBSD_CONFIG="$absent_config" \
	python3 "$repo/tools/menuconfig.py" --defaults --output "$default_config"
if grep '^ZEDBSD_USER_PROGRAMS' "$default_config" | grep -Fq 'bcm43455-firmware'; then
	echo 'bcm43455 firmware must be default-off' >&2
	exit 1
fi
test ! -e "$default_cache"
default_data=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$default_config" \
	--eval='bcm43455-print-default-data:;@printf "%s\n" "$(ZEDBSD_USERLAND_DATA_INPUTS)"' \
	bcm43455-print-default-data)
if grep -Fq '43455-sdio' <<<"$default_data"; then
	echo 'ordinary default image depends on bcm43455 firmware' >&2
	exit 1
fi
test ! -e "$default_cache"
selecting=$(grep -l '^ZEDBSD_USER_PROGRAMS.*[ =]bcm43455-firmware\( \|$\)' "$repo"/config/*/*.mk "$repo"/config/*.mk 2>/dev/null || true)
test "$selecting" = "$repo/config/ci/config-rpi4.mk" || {
	echo "bcm43455 firmware selected by an unexpected configuration: $selecting" >&2
	exit 1
}

# The fixture override exists only for its single hermetic goal.
if make -C "$repo" --no-print-directory ZEDBSD_CONFIG="$absent_config" \
	bcm43455-firmware-fixture-cache list-user-programs \
	>"$temporary/mixed-goal.log" 2>&1; then
	echo 'fixture goal unexpectedly accepted a second goal' >&2
	exit 1
fi
grep -Fq 'must be the only requested goal' "$temporary/mixed-goal.log"

nonfree_fixture_revision=bcm43455-fixture-nonfree
linux_fixture_revision=bcm43455-fixture-linux
fixture=$temporary/mirror
nonfree_tree=$fixture/nonfree/$nonfree_fixture_revision
mkdir -p "$nonfree_tree/debian/added-firmware/cypress" \
	"$nonfree_tree/debian/added-firmware/brcm" "$fixture/linux"
printf 'bcm43455 fixture firmware\n' >"$nonfree_tree/debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin"
printf 'bcm43455 fixture clm\n' >"$nonfree_tree/debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob"
printf 'bcm43455 fixture nvram\n' >"$nonfree_tree/debian/added-firmware/brcm/brcmfmac43455-sdio.txt"
printf 'bcm43455 fixture copyright\n' >"$nonfree_tree/debian/copyright"
printf 'bcm43455 fixture licence\n' >"$fixture/linux/LICENCE.cypress"
printf 'bcm43455 fixture gpl\n' >"$fixture/linux/GPL-2"
fixture_entry() {
	local name=$1 source=$2 path=$3 file=$4
	printf '%s|%s|%s|%s|%s' "$name" "$source" "$path" \
		"$(wc -c <"$file" | tr -d '[:space:]')" \
		"$(sha256sum "$file" | awk '{print $1}')"
}
fixture_files="$(fixture_entry cyfmac43455-sdio-minimal.bin nonfree debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin "$nonfree_tree/debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin") \
$(fixture_entry cyfmac43455-sdio.clm_blob nonfree debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob "$nonfree_tree/debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob") \
$(fixture_entry brcmfmac43455-sdio.txt nonfree debian/added-firmware/brcm/brcmfmac43455-sdio.txt "$nonfree_tree/debian/added-firmware/brcm/brcmfmac43455-sdio.txt") \
$(fixture_entry copyright nonfree debian/copyright "$nonfree_tree/debian/copyright") \
$(fixture_entry LICENCE.cypress linux LICENCE.cypress "$fixture/linux/LICENCE.cypress") \
$(fixture_entry GPL-2 linux GPL-2 "$fixture/linux/GPL-2")"
names='cyfmac43455-sdio-minimal.bin cyfmac43455-sdio.clm_blob brcmfmac43455-sdio.txt copyright LICENCE.cypress GPL-2'

run_package() {
	local cache_root=$1
	local fetch_command=$2
	local nonfree_base=${3:-file://$fixture/nonfree}
	local linux_base=${4:-file://$fixture/linux}
	FETCH_LOG=$fetch_log make -C "$repo" --no-print-directory \
		ZEDBSD_CONFIG="$absent_config" \
		BCM43455_FIRMWARE_NONFREE_BASE_URL="$nonfree_base" \
		BCM43455_FIRMWARE_NONFREE_REVISION="$nonfree_fixture_revision" \
		BCM43455_FIRMWARE_LINUX_BASE_URL="$linux_base" \
		BCM43455_FIRMWARE_LINUX_REVISION="$linux_fixture_revision" \
		BCM43455_FIRMWARE_LINUX_URL_SUFFIX= \
		BCM43455_FIRMWARE_FILES="$fixture_files" \
		BCM43455_FIRMWARE_CACHE_ROOT="$cache_root" \
		BCM43455_FIRMWARE_FETCH="$fetch_command" \
		bcm43455-firmware-fixture-cache
}

cache_root=$temporary/cache
cache_name=$nonfree_fixture_revision-$linux_fixture_revision
: >"$fetch_log"
run_package "$cache_root" "$fetcher"
cache=$cache_root/$cache_name
test "$(wc -l <"$fetch_log" | tr -d '[:space:]')" = 6
grep -Fq "file://$fixture/nonfree/$nonfree_fixture_revision/debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin" "$fetch_log"
grep -Fq "file://$fixture/nonfree/$nonfree_fixture_revision/debian/added-firmware/brcm/brcmfmac43455-sdio.txt" "$fetch_log"
grep -Fq "file://$fixture/linux/LICENCE.cypress" "$fetch_log"
grep -Fq "file://$fixture/linux/GPL-2" "$fetch_log"
cmp -s "$cache/cyfmac43455-sdio-minimal.bin" "$nonfree_tree/debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin"
cmp -s "$cache/cyfmac43455-sdio.clm_blob" "$nonfree_tree/debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob"
cmp -s "$cache/brcmfmac43455-sdio.txt" "$nonfree_tree/debian/added-firmware/brcm/brcmfmac43455-sdio.txt"
cmp -s "$cache/copyright" "$nonfree_tree/debian/copyright"
cmp -s "$cache/LICENCE.cypress" "$fixture/linux/LICENCE.cypress"
cmp -s "$cache/GPL-2" "$fixture/linux/GPL-2"

# A complete cache is reusable offline.  A missing cache, unsafe path,
# unexpected file, or any corrupt object fails without replacing state.
: >"$fetch_log"
run_package "$cache_root" "$fail_fetcher"
test ! -s "$fetch_log"

missing_root=$temporary/missing
if run_package "$missing_root" "$fail_fetcher" >"$temporary/missing.log" 2>&1; then
	echo 'missing offline bcm43455 cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unexpected firmware network fetch' "$temporary/missing.log"
test ! -e "$missing_root/$cache_name"

snapshot=$temporary/snapshot
cp -a "$cache" "$snapshot"
for corrupt_name in $names; do
	corrupt_root=$temporary/corrupt-$corrupt_name
	mkdir -p "$corrupt_root"
	cp -a "$snapshot" "$corrupt_root/$cache_name"
	printf 'corrupt\n' >>"$corrupt_root/$cache_name/$corrupt_name"
	if run_package "$corrupt_root" "$fail_fetcher" >"$temporary/corrupt.log" 2>&1; then
		echo "corrupt bcm43455 cache unexpectedly accepted: $corrupt_name" >&2
		exit 1
	fi
	grep -Eq 'size mismatch|SHA-256 mismatch' "$temporary/corrupt.log"
done

# Same size, different bytes: only the hash can catch it.
flip_root=$temporary/flip
mkdir -p "$flip_root"
cp -a "$snapshot" "$flip_root/$cache_name"
printf 'bcm43455 fixture nvraX\n' >"$flip_root/$cache_name/brcmfmac43455-sdio.txt"
if run_package "$flip_root" "$fail_fetcher" >"$temporary/flip.log" 2>&1; then
	echo 'same-size corrupt bcm43455 cache unexpectedly accepted' >&2
	exit 1
fi
grep -Fq 'SHA-256 mismatch' "$temporary/flip.log"

extra_root=$temporary/extra
mkdir -p "$extra_root"
cp -a "$snapshot" "$extra_root/$cache_name"
: >"$extra_root/$cache_name/unexpected"
if run_package "$extra_root" "$fail_fetcher" >"$temporary/extra.log" 2>&1; then
	echo 'bcm43455 cache with an extra object unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'cache must contain exactly 6 files' "$temporary/extra.log"

link_root=$temporary/link-file
mkdir -p "$link_root/$cache_name"
cp -a "$snapshot/." "$link_root/$cache_name/"
mv "$link_root/$cache_name/GPL-2" "$link_root/GPL-2.real"
ln -s ../GPL-2.real "$link_root/$cache_name/GPL-2"
if run_package "$link_root" "$fail_fetcher" >"$temporary/link-file.log" 2>&1; then
	echo 'bcm43455 cache with a symlinked file unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'missing or unsafe cache file' "$temporary/link-file.log"

unsafe_root=$temporary/unsafe
mkdir -p "$unsafe_root"
ln -s "$snapshot" "$unsafe_root/$cache_name"
if run_package "$unsafe_root" "$fail_fetcher" >"$temporary/unsafe.log" 2>&1; then
	echo 'symlink bcm43455 cache unexpectedly succeeded' >&2
	exit 1
fi
grep -Fq 'unsafe cache path' "$temporary/unsafe.log"

# A partial fetch (the firmware-nonfree tree reachable, the licences not)
# publishes nothing.
partial_root=$temporary/partial-cache
mkdir -p "$partial_root"
if run_package "$partial_root" "$fetcher" "file://$fixture/nonfree" \
	"file://$temporary/no-such-mirror" >"$temporary/partial.log" 2>&1; then
	echo 'partial bcm43455 acquisition unexpectedly succeeded' >&2
	exit 1
fi
test ! -e "$partial_root/$cache_name"
test -z "$(find "$partial_root" -mindepth 1 -print -quit)"

# The selected rootfs mapping: three chip files, two licences, the
# copyright record, and the manifest.
data_mapping=$(make -C "$repo" --no-print-directory -s \
	ZEDBSD_CONFIG="$absent_config" ZEDBSD_PLATFORM=rpi4 \
	ZEDBSD_ARCHITECTURE=arm64 ZEDBSD_BOARD=rpi4 ZEDBSD_VARIANT=default \
	ZEDBSD_USER_PROGRAMS=bcm43455-firmware \
	--eval='bcm43455-print-firmware-data:;@printf "%s\n" "$(BCM43455_FIRMWARE_DATA)"' \
	bcm43455-print-firmware-data)
test "$(grep -o -- '/[^ =]*=' <<<"$data_mapping" | wc -l | tr -d '[:space:]')" = 7
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/lib/firmware/cypress/cyfmac43455-sdio-minimal\.bin=.*/cyfmac43455-sdio-minimal\.bin$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/lib/firmware/cypress/cyfmac43455-sdio\.clm_blob=.*/cyfmac43455-sdio\.clm_blob$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/lib/firmware/brcm/brcmfmac43455-sdio\.txt=.*/brcmfmac43455-sdio\.txt$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/usr/share/licenses/bcm43455-firmware/LICENCE\.cypress=.*/LICENCE\.cypress$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/usr/share/licenses/bcm43455-firmware/GPL-2=.*/GPL-2$'
tr ' ' '\n' <<<"$data_mapping" | grep -Eq '^/usr/share/licenses/bcm43455-firmware/firmware-nonfree\.copyright=.*/copyright$'
grep -Fq '/usr/share/zedbsd/packages/bcm43455-firmware.manifest=userland/firmware/bcm43455/bcm43455-firmware.manifest' <<<"$data_mapping"

# When this package's verified cache is at hand: the installed plain licence equals that record's licence paragraph (DEP-5
# indentation, its trailing separator and the plain file's two "### "
# heading marks aside), and the chip files have the shapes the driver
# checks: the firmware's version string with the firmware supplicant, the
# CLM's BLOB header with the world locale, and an NVRAM text without a
# country code; and the copyright record puts the three files under the
# Cypress licence by its last matching stanza.
real_cache=$repo/build/sources/firmware/bcm43455/$nonfree_revision-$linux_revision
if test -d "$real_cache" && test ! -L "$real_cache"; then
	test "$(sha256sum "$real_cache/cyfmac43455-sdio-minimal.bin" | awk '{print $1}')" = "$firmware_hash"
	test "$(sha256sum "$real_cache/cyfmac43455-sdio.clm_blob" | awk '{print $1}')" = "$clm_hash"
	test "$(sha256sum "$real_cache/brcmfmac43455-sdio.txt" | awk '{print $1}')" = "$nvram_hash"
	test "$(sha256sum "$real_cache/copyright" | awk '{print $1}')" = "$copyright_hash"
	test "$(sha256sum "$real_cache/LICENCE.cypress" | awk '{print $1}')" = "$licence_hash"
	test "$(sha256sum "$real_cache/GPL-2" | awk '{print $1}')" = "$gpl_hash"
	awk '
		previous == "" && /^License: binary-redist-Cypress$/ { on = 1; previous = $0; next }
		on && /^[^ ]/ { exit }
		on { sub(/^ /, ""); if ($0 == ".") $0 = ""; print }
		{ previous = $0 }
	' "$real_cache/copyright" >"$temporary/dep5-cypress.txt"
	sed '${/^$/d;}' "$temporary/dep5-cypress.txt" >"$temporary/dep5-cypress.trimmed"
	sed 's/^### //' "$real_cache/LICENCE.cypress" >"$temporary/licence-plain.txt"
	cmp -s "$temporary/dep5-cypress.trimmed" "$temporary/licence-plain.txt" || {
		echo 'LICENCE.cypress differs from the copyright record Cypress paragraph' >&2
		exit 1
	}
	grep -Fq 'GNU GENERAL PUBLIC LICENSE' "$real_cache/GPL-2"
	grep -Fq 'Version 2, June 1991' "$real_cache/GPL-2"
	python3 -I - "$real_cache" <<'EOF'
import os
import re
import sys

directory = sys.argv[1]
# DEP-5: the last Files stanza whose pattern matches a path is effective
# ("*" also matches "/").
record = open(os.path.join(directory, "copyright"), encoding="utf-8").read()
for path in ("debian/added-firmware/cypress/cyfmac43455-sdio-minimal.bin",
	     "debian/added-firmware/cypress/cyfmac43455-sdio.clm_blob",
	     "debian/added-firmware/brcm/brcmfmac43455-sdio.txt"):
	effective = None
	for paragraph in record.split("\n\n"):
		files = re.match(r"Files:(.*?)\n(?=\S)", paragraph + "\n", re.S)
		licence = re.search(r"^License: (.*)$", paragraph, re.M)
		if not files or not licence:
			continue
		for pattern in files.group(1).split():
			expression = "^" + re.escape(pattern).replace(r"\*", ".*").replace(r"\?", ".") + "$"
			if re.match(expression, path):
				effective = (pattern, licence.group(1))
				break
	if effective != ("debian/added-firmware/*/*43455*", "binary-redist-Cypress"):
		sys.exit("unexpected effective copyright stanza for %s: %s" % (path, effective))
firmware = open(os.path.join(directory, "cyfmac43455-sdio-minimal.bin"), "rb").read()
start = firmware.find(b"43455c0-roml/")
if start < 0:
	sys.exit("firmware without its 43455c0-roml version string")
version = firmware[start:firmware.index(b"\0", start)].decode("ascii")
if "-idsup-" not in version or "Version: 7.45.241" not in version:
	sys.exit("unexpected firmware version string: %s" % version)
clm = open(os.path.join(directory, "cyfmac43455-sdio.clm_blob"), "rb").read()
if not clm.startswith(b"BLOB") or b"CLM DATA" not in clm or b"RPi.4" not in clm or b"XX" not in clm:
	sys.exit("CLM without its BLOB header, RPi.4 name or world locale")
nvram = open(os.path.join(directory, "brcmfmac43455-sdio.txt"), "rb").read().decode("ascii")
keys = [line.split("=", 1)[0] for line in nvram.splitlines() if line.strip() and not line.startswith("#")]
if "ccode" in keys or "regrev" in keys:
	sys.exit("NVRAM sets a country; the world locale is selected by the driver")
for key in ("boardtype", "macaddr", "xtalfreq"):
	if key not in keys:
		sys.exit("NVRAM without %s" % key)
print("bcm43455 firmware package: %s" % version.split(" Version: ")[1].split(" ")[0])
EOF
	echo 'bcm43455 firmware package: licence and copyright record match'
else
	echo 'bcm43455 firmware package: licence cross-check skipped (no verified cache)'
fi

echo 'bcm43455 firmware package: PASS'
