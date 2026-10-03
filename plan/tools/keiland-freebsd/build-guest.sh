#!/bin/sh
# build-guest.sh [--force] [OUT] -- the FreeBSD 15.1 guest image for the native Keiland tests (WS137).
#
# Fetches the official FreeBSD-15.1-RELEASE-amd64-BASIC-CLOUDINIT-ufs.qcow2.xz, checks it against the official
# CHECKSUM.SHA256 (whose line must equal the one WS109 recorded in plan/history/ws109/q550/CHECKSUM.SHA256), boots it
# once with a NoCloud seed (root and kei by this guest's own SSH key only), lets the first boot finish (growfs,
# nuageinit, the first-boot package upgrade and its reboot), installs the native build packages with pkg and powers
# it off.  The result OUT/guest.qcow2 is the read-only base of plan/tools/keiland-freebsd/guest.sh.
# An existing OUT/guest.qcow2 is reused unless --force is given.  The guest is reached only through 127.0.0.1's
# forwarded port and QMP; its serial port is not connected and no console or serial log is read.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

FORCE=0
if [ "${1:-}" = --force ]; then
	FORCE=1
	shift
fi
TOOLS=$(cd "$(dirname -- "$0")" && pwd)
ROOT=$(cd "$TOOLS/../../.." && pwd)
OUT=${1:-$ROOT/build/keiland-freebsd/guest}
SIZE=${SIZE:-24G}
RELEASE=15.1-RELEASE
NAME=FreeBSD-$RELEASE-amd64-BASIC-CLOUDINIT-ufs.qcow2.xz
SITE=${SITE:-https://download.freebsd.org/releases/VM-IMAGES/$RELEASE/amd64/Latest}
RECORD=$ROOT/plan/history/ws109/q550/CHECKSUM.SHA256
# userland/desktop/README.freebsd.md's list, pkgconf for meson (WS109 q551) and the guest's own tests.
PACKAGES=${PACKAGES:-gmake python3 meson ninja pkgconf vulkan-headers vulkan-loader libdrm mesa-dri seatd}

if [ -e "$OUT/guest.qcow2" ] && [ "$FORCE" != 1 ]; then
	echo "build-guest.sh: $OUT/guest.qcow2 exists (--force rebuilds)"
	exit 0
fi
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

# The official checksum list, and its line for the image must be the recorded one.
curl -fsSL --retry 2 -o "$OUT/CHECKSUM.SHA256.tmp" "$SITE/CHECKSUM.SHA256"
mv "$OUT/CHECKSUM.SHA256.tmp" "$OUT/CHECKSUM.SHA256"
line=$(grep -F "($NAME)" "$OUT/CHECKSUM.SHA256")
if [ "$line" != "$(grep -F "($NAME)" "$RECORD")" ]; then
	echo "build-guest.sh: the official CHECKSUM.SHA256 line differs from $RECORD" >&2
	exit 1
fi
sha=${line##* = }
if [ ! -e "$OUT/$NAME" ] || [ "$(sha256sum "$OUT/$NAME" | cut -d' ' -f1)" != "$sha" ]; then
	if [ ! -e "$OUT/$NAME.part" ] || [ "$(sha256sum "$OUT/$NAME.part" | cut -d' ' -f1)" != "$sha" ]; then
		curl -fSL --retry 2 -o "$OUT/$NAME.part" "$SITE/$NAME"
	fi
	mv "$OUT/$NAME.part" "$OUT/$NAME"
fi
echo "$sha  $OUT/$NAME" | sha256sum -c -

# The guest's own key; the private half never leaves OUT.
[ -e "$OUT/id_ed25519" ] || ssh-keygen -q -t ed25519 -N '' -C keiland-freebsd-guest -f "$OUT/id_ed25519"
key=$(cat "$OUT/id_ed25519.pub")
rm -rf "$OUT/seed"
mkdir -p "$OUT/seed"
printf 'instance-id: keiland-freebsd-%s\nlocal-hostname: keiland-freebsd\n' "$(date +%Y%m%d%H%M%S)" > "$OUT/seed/meta-data"
cat > "$OUT/seed/user-data" <<EOF
#cloud-config
hostname: keiland-freebsd
ssh_pwauth: false
users:
  - name: kei
    gecos: Keiland test user
    groups: wheel,video
    shell: /bin/sh
    ssh_authorized_keys:
      - $key
runcmd:
  - mkdir -p /root/.ssh
  - chmod 700 /root/.ssh
  - echo "$key" > /root/.ssh/authorized_keys
  - chmod 600 /root/.ssh/authorized_keys
  - echo "PermitRootLogin prohibit-password" >> /etc/ssh/sshd_config
  - sysrc sshd_enable=YES
  - service sshd restart
EOF
xorriso -as mkisofs -quiet -o "$OUT/seed.iso" -V cidata -J -r "$OUT/seed/user-data" "$OUT/seed/meta-data"

rm -f "$OUT/guest.qcow2.new"
xz -dc "$OUT/$NAME" > "$OUT/guest.qcow2.new"
qemu-img resize -q -f qcow2 "$OUT/guest.qcow2.new" "$SIZE"

# The preparation guest has its own run directory and port, apart from a running test guest.
export GUEST_DIR="$OUT" GUEST_IMAGE="$OUT/guest.qcow2.new" GUEST_KEY="$OUT/id_ed25519"
export GUEST_RUN="$OUT/prepare" SSH_PORT="${PREPARE_PORT:-2236}"
guest() {
	python3 "$TOOLS/guest.py" "$@"
}
trap 'guest stop >/dev/null 2>&1 || true' EXIT INT TERM
guest prepare "$OUT/seed.iso"
guest shot "$OUT/first-boot.png"
GUEST_COMMAND_TIMEOUT=1800 guest ssh "env ASSUME_ALWAYS_YES=yes pkg install -y $PACKAGES" > "$OUT/pkg-install.txt" 2>&1 || {
	echo "build-guest.sh: pkg install failed; see $OUT/pkg-install.txt" >&2
	exit 1
}
guest ssh 'pkg clean -ay >/dev/null; pkg query "%n %v %L" | sort' > "$OUT/packages.txt"
guest ssh 'freebsd-version -ku; uname -m; cc --version | head -1; gmake --version | head -1; python3 --version' \
	> "$OUT/versions.txt"
guest stop
trap - EXIT INT TERM
rm -rf "$OUT/prepare"
mv "$OUT/guest.qcow2.new" "$OUT/guest.qcow2"
echo "build-guest.sh: $OUT/guest.qcow2"
cat "$OUT/versions.txt"
