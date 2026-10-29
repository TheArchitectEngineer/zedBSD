#!/bin/sh
# WS307 p008: boots, through OVMF, a disk whose kernel and logo are on the UFS root (kernel=rootpart:/boot/vmunix,
# logo=rootpart:/boot/logo.ppm) and whose ESP holds only the loader and zedbsd.cfg -- the installed system's form.
# The disk is made from the staged root, the kernel, the loader and the logo of BUILD (make the four targets with
# plan/ws307/tests/config-amd64-p008.mk first).  The ESP is FAT32; the root's PARTUUID is new for each run.
#   sh plan/ws307/tests/p008-root-boot.sh BUILD [CASE]
# CASE: boot (the login prompt, default), fat (the kernel and the logo on the FAT volume, as today's images: the
# login prompt), wrong-uuid (the loader names a root that is not there and stops: the
# screen shows "A64 KERN UFS root partition not found"), no-kernel (no /boot/vmunix on the root: "A64 KERN UFS not
# found"), duplicate (a second disk with the same PARTUUID: "A64 KERN UFS duplicate PARTUUID").  The three failure
# cases end with a screen shot (SHOT) to look at.  VIDEO=WxH adds video=.  The last line is "p008-root-boot: CASE PASS|FAIL OUT".  Output goes to a new build/ws307-test/p008-boot.*.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
B=${1:?usage: p008-root-boot.sh BUILD [CASE]}
CASE=${2:-boot}
NOCT=build/NoctLang/build-static/noct
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/p008-boot.XXXXXX)

# The root: the staged tree (hard links, only files are added) with the kernel and the logo in /boot.
cp -al "$B/rootfs" "$out/tree"
if [ "$CASE" != no-kernel ]; then
	cp "$B/vmunix" "$out/tree/boot/vmunix"
fi
cp "$B/boot-logo.ppm" "$out/tree/boot/logo.ppm"
timeout 600 "$NOCT" --path=tools/build tools/build/make-arch-overlay-ufs.noct --backend "$(pwd)/build/zedimage-host" \
	--force --profile amd64 --output "$out/root.ufs" --tree "$out/tree" --size-mib 1024 --min-inodes 65536 \
	> "$out/root.txt" 2>&1

# The ESP: the loader as the fallback loader, and zedbsd.cfg at the root of the volume.
root_uuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
cfg_uuid=$root_uuid
if [ "$CASE" = wrong-uuid ]; then
	cfg_uuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
fi
printf 'kernel=rootpart:/boot/vmunix\nrootpart=PARTUUID=%s\nlogo=rootpart:/boot/logo.ppm\n' "$cfg_uuid" \
	> "$out/zedbsd.cfg"
if [ -n "${VIDEO:-}" ]; then
	printf 'video=%s\n' "$VIDEO" >> "$out/zedbsd.cfg"
fi
truncate -s 128M "$out/esp.img"
/usr/sbin/mkfs.fat -F 32 -s 1 -n EFI "$out/esp.img" > "$out/esp.txt"
mmd -i "$out/esp.img" ::/EFI ::/EFI/BOOT
mcopy -i "$out/esp.img" "$B/uefi/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
if [ "$CASE" = fat ]; then
	# Today's form: the kernel and the logo on the FAT volume beside zedbsd.cfg, the root named by PARTUUID.
	printf 'kernel=vmunix\nrootpart=PARTUUID=%s\nlogo=logo.ppm\n' "$root_uuid" > "$out/zedbsd.cfg"
	mcopy -i "$out/esp.img" "$B/vmunix" ::/vmunix
	mcopy -i "$out/esp.img" "$B/boot-logo.ppm" ::/logo.ppm
fi
mcopy -i "$out/esp.img" "$out/zedbsd.cfg" ::/zedbsd.cfg

# The disk: GPT with the ESP and the root, and nothing else.
truncate -s 1200M "$out/disk.img"
/usr/sbin/sgdisk -o -n 1:2048:+128M -t 1:EF00 -c 1:'EFI System' \
	-n 2:0:+1024M -t 2:516E7CB6-6ECF-11D6-8FF8-00022D09712B -u "2:$root_uuid" -c 2:'zedBSD installed root' \
	"$out/disk.img" > "$out/sgdisk.txt"
dd if="$out/esp.img" of="$out/disk.img" bs=1M seek=1 conv=notrunc status=none
dd if="$out/root.ufs" of="$out/disk.img" bs=1M seek=129 conv=notrunc status=none

# The failures stop the loader with a line on the firmware's console, which boot-test.sh's reader (the kernel's
# 8x16 font) cannot read: those cases take a screen shot after 25 s for a person (or an agent) to look at.
shot() {
	cp /usr/share/OVMF/OVMF_VARS_4M.fd "$out/vars.fd"
	second=
	if [ -n "${1:-}" ]; then
		second="-drive if=none,id=other,file=$1,format=raw,snapshot=on -device nvme,serial=zedbsd-other,drive=other"
	fi
	# shellcheck disable=SC2086
	timeout 60 qemu-system-x86_64 -machine q35 -m 2G -smp 2 -accel kvm -cpu host \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive "if=pflash,format=raw,file=$out/vars.fd" \
		-drive "if=none,id=boot,file=$out/disk.img,format=raw,snapshot=on" \
		-device nvme,serial=zedbsd-boot,drive=boot,bootindex=1 $second \
		-vga std -display none -no-reboot -monitor "unix:$out/monitor.sock,server,nowait" > "$out/qemu.txt" 2>&1 &
	sleep 25
	python3 - "$out/monitor.sock" "$(pwd)/$out/screen.ppm" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
time.sleep(0.5)
s.recv(65536)
s.send(("screendump " + sys.argv[2] + "\n").encode())
time.sleep(2)
s.recv(65536)
s.send(b"quit\n")
PY
	python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$out/screen.ppm" "$out/screen.png"
	echo "p008-root-boot: $CASE SHOT $out/screen.png"
}

case "$CASE" in
wrong-uuid | no-kernel)
	shot
	exit 0
	;;
duplicate)
	# A second disk with the same partitions (as a dd copy would have): the loader stops with
	# "A64 KERN UFS duplicate PARTUUID" rather than let the kernel refuse the root.
	cp --sparse=always "$out/disk.img" "$out/copy.img"
	shot "$out/copy.img"
	exit 0
	;;
esac

# Boots it and reads the login prompt.
status=0
BOOT_MODE=uefi-nvme OUTPUT="$out/boot" BOOT_TIMEOUT=${BOOT_TIMEOUT:-180} \
	timeout 400 bash plan/tools/boot-test.sh "$out/disk.img" > "$out/boot-test.txt" 2>&1 || status=$?
if [ "$status" = 0 ]; then
	echo "p008-root-boot: $CASE PASS $out"
else
	echo "p008-root-boot: $CASE FAIL $out (boot-test status $status)"
fi
