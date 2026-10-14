#!/bin/sh
# Fetch every firmware package (and the Raspberry Pi 4 boot firmware) through
# its fixture goal from a local copy of the zedBSD firmware repository, with
# the upstream made unreachable, and check that the published cache holds
# the declared bytes.  Then fetch two packages with the repository made
# unreachable, which must fall back to the real upstream.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: check-repository-fetch.sh FIRMWARE-REPO OUTDIR
# Run from the top of the zedBSD tree.  OUTDIR must not exist; nothing is
# removed.
set -eu

repo=$(cd "$1" && pwd)
out=$2
if test -e "$out"; then
	echo "check-repository-fetch: $out exists" >&2
	exit 1
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)
inventory=plan/ws233/tests/firmware-inventory.txt
absent_config=$out/absent-config.mk
nowhere=file:///nonexistent/ws233-upstream
fetcher=$out/fetch
cat >"$fetcher" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$FETCH_LOG"
exec curl --fail --location --silent --show-error "$@"
EOF
chmod +x "$fetcher"

run() {
	name=$1
	shift
	FETCH_LOG=$out/$name.fetch.log make --no-print-directory -s \
		ZEDBSD_CONFIG="$absent_config" "$@" >"$out/$name.log" 2>&1 || {
		echo "FAIL $name (make)"; cat "$out/$name.log"; return 1; }
}

# check PACKAGE CACHE-DIR: every inventory file of PACKAGE that the build
# fetches is in CACHE-DIR with the declared size and SHA-256.
check() {
	package=$1
	cache=$2
	count=0
	set -f
	for entry in $(grep "^$package|" "$inventory"); do
		old_ifs=$IFS; IFS='|'; set -- $entry; IFS=$old_ifs
		test "$2" = COPYING.linux && continue
		file=$cache/$2
		if test "$(wc -c <"$file" | tr -d '[:space:]')" != "$4" ||
			test "$(sha256sum "$file" | awk '{print $1}')" != "$5"; then
			echo "FAIL $package/$2"
			return 1
		fi
		count=$((count + 1))
	done
	set +f
	echo "OK   $package: $count files"
}

# 1. From the repository only.
run i915 I915_FIRMWARE_BASE_URL=$nowhere I915_FIRMWARE_REVISION=r \
	I915_FIRMWARE_ZEDBSD_URL=file://$repo/i915 \
	I915_FIRMWARE_CACHE_ROOT=$out/cache/i915 I915_FIRMWARE_FETCH=$fetcher \
	i915-firmware-fixture-cache
check i915 "$out/cache/i915/r"
run intelax211 INTEL_AX211_FIRMWARE_BASE_URL=$nowhere INTEL_AX211_FIRMWARE_REVISION=r \
	INTEL_AX211_FIRMWARE_ZEDBSD_URL=file://$repo/intelax211 \
	INTEL_AX211_FIRMWARE_CACHE_ROOT=$out/cache/intelax211 INTEL_AX211_FIRMWARE_FETCH=$fetcher \
	intelax211-firmware-fixture-cache
check intelax211 "$out/cache/intelax211/r"
run intelbt INTELBT_FIRMWARE_BASE_URL=$nowhere INTELBT_FIRMWARE_REVISION=r \
	INTELBT_FIRMWARE_ZEDBSD_URL=file://$repo/intelbt \
	INTELBT_FIRMWARE_CACHE_ROOT=$out/cache/intelbt INTELBT_FIRMWARE_FETCH=$fetcher \
	intelbt-firmware-fixture-cache
check intelbt "$out/cache/intelbt/r"
run rtl8822b RTL8822B_FIRMWARE_MIRROR_BASE_URL=$nowhere RTL8822B_FIRMWARE_MIRROR_REVISION=r \
	RTL8822B_FIRMWARE_ZEDBSD_URL=file://$repo/rtl8822b \
	RTL8822B_FIRMWARE_CACHE_ROOT=$out/cache/rtl8822b RTL8822B_FIRMWARE_FETCH=$fetcher \
	rtl8822b-firmware-fixture-cache
check rtl8822b "$out/cache/rtl8822b/r"
run brcmbt BRCMBT_FIRMWARE_BLUEZ_BASE_URL=$nowhere BRCMBT_FIRMWARE_BLUEZ_REVISION=r \
	BRCMBT_FIRMWARE_LINUX_BASE_URL=$nowhere BRCMBT_FIRMWARE_LINUX_REVISION=r \
	BRCMBT_FIRMWARE_ZEDBSD_URL=file://$repo/brcmbt \
	BRCMBT_FIRMWARE_CACHE_ROOT=$out/cache/brcmbt BRCMBT_FIRMWARE_FETCH=$fetcher \
	brcmbt-firmware-fixture-cache
check brcmbt "$out/cache/brcmbt/r-r"
run bcm43455 BCM43455_FIRMWARE_NONFREE_BASE_URL=$nowhere BCM43455_FIRMWARE_NONFREE_REVISION=r \
	BCM43455_FIRMWARE_LINUX_BASE_URL=$nowhere BCM43455_FIRMWARE_LINUX_REVISION=r \
	BCM43455_FIRMWARE_ZEDBSD_URL=file://$repo/bcm43455 \
	BCM43455_FIRMWARE_CACHE_ROOT=$out/cache/bcm43455 BCM43455_FIRMWARE_FETCH=$fetcher \
	bcm43455-firmware-fixture-cache
check bcm43455 "$out/cache/bcm43455/r-r"
run rpi4-boot ZEDBSD_CONFIG=config/ci/config-rpi4.mk RPI4_BOOT_FIRMWARE_BASE_URL=$nowhere RPI4_BOOT_FIRMWARE_REVISION=r \
	RPI4_BOOT_FIRMWARE_ZEDBSD_URL=file://$repo/rpi4-boot \
	RPI4_BOOT_FIRMWARE_CACHE_ROOT=$out/cache/rpi4-boot RPI4_BOOT_FIRMWARE_FETCH=$fetcher \
	rpi4-boot-firmware-fixture-cache
check rpi4-boot "$out/cache/rpi4-boot/r"
if grep -v "file://$repo/" "$out"/*.fetch.log; then
	echo "FAIL a fetch did not use the repository"
	exit 1
fi
echo "OK   $(cat "$out"/*.fetch.log | wc -l | tr -d ' ') fetches, all from the repository"

# 2. The repository unreachable: the upstream supplies the same bytes.
run fallback-intelbt INTELBT_FIRMWARE_ZEDBSD_URL=$nowhere/intelbt \
	INTELBT_FIRMWARE_URL_SUFFIX='?id=dc85ccedc9c973682fbcf4d628ca61174bcc3120' \
	INTELBT_FIRMWARE_CACHE_ROOT=$out/fallback/intelbt INTELBT_FIRMWARE_FETCH=$fetcher \
	intelbt-firmware-fixture-cache
check intelbt "$out/fallback/intelbt/dc85ccedc9c973682fbcf4d628ca61174bcc3120"
grep -c 'could not fetch' "$out/fallback-intelbt.log" >/dev/null
run fallback-rpi4-boot ZEDBSD_CONFIG=config/ci/config-rpi4.mk RPI4_BOOT_FIRMWARE_ZEDBSD_URL=$nowhere/rpi4-boot \
	RPI4_BOOT_FIRMWARE_CACHE_ROOT=$out/fallback/rpi4-boot RPI4_BOOT_FIRMWARE_FETCH=$fetcher \
	rpi4-boot-firmware-fixture-cache
check rpi4-boot "$out/fallback/rpi4-boot/3d301dd924bcd758a4c8cb19fe8531031f033f43"
echo "OK   fallback to upstream: $(cat "$out/fallback-intelbt.log" "$out/fallback-rpi4-boot.log" | grep -c "could not fetch") files"

# 3. A second run reuses the verified caches without fetching.
run reuse I915_FIRMWARE_BASE_URL=$nowhere I915_FIRMWARE_REVISION=r \
	I915_FIRMWARE_ZEDBSD_URL=$nowhere \
	I915_FIRMWARE_CACHE_ROOT=$out/cache/i915 I915_FIRMWARE_FETCH=false \
	i915-firmware-fixture-cache
echo "OK   cache reuse"
