#!/bin/bash
# zedBSD boot test: UEFI boot from a USB stick, photographed at the login prompt.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The machine boots the disk image as a USB mass storage device through OVMF,
# the way the physical machines are booted, and the screen is captured from the
# emulator's framebuffer.  The console draws to the framebuffer, so a picture is
# what a person at the machine would see; nothing here reads a serial port.
#
#   plan/tools/boot-test.sh [IMAGE]
#
#   IMAGE          disk image to boot, as the operand or in the environment
#                  (default build/amd64/hdd-image.img)
#   OUTPUT         where the screenshot and logs go (default build/boot-test)
#   BOOT_TEST_WORK where the emulator's working files go: the stick's copy, the
#                  firmware variables, the monitor socket and the screen
#                  frames (default a new directory under TMPDIR, else /tmp)
#   BOOT_TIMEOUT   seconds to wait for the login prompt (default 180)
#   QEMU           emulator to run (default depends on BOOT_MODE)
#   BOOT_MODE      uefi-nvme (default), uefi-usb, bios-ide, raspi4b,
#                  riscv64-virt, riscv64-uboot or riscv32-virt
#   BOOT_PATTERN   what the screen must show, one regular expression per line;
#                  every one must be seen on some screen (default the login
#                  prompt, ^(?:\S+ )?login: ?)
#   BOOT_THEN      steps after the patterns were seen, one per line:
#                  "type:TEXT" types TEXT and a return on the guest's serial
#                  line, "expect:REGEX" waits for the screen to show REGEX;
#                  the last screen is OUTPUT/login-after.png
#   BOOT_INTERVAL  seconds between two looks at the screen (default 2, 0.5 in
#                  the riscv64 modes)
#   QEMU_NO_KVM    1 runs the amd64 guest on TCG (-cpu max) even when /dev/kvm
#                  is usable; otherwise it runs under KVM (-cpu host) when the
#                  host offers it (plan/tools/guest/qemu-accel.sh)
#
# The bios-ide and raspi4b guests always run on TCG.
#
# BOOT_MODE=bios-ide boots the image the way i386 machines are booted: the
# firmware is the PC BIOS and the disk is on IDE.  Those kernels put their
# console in the VGA text buffer rather than a linear framebuffer, which the
# screen reader handles; the picture is still what the screen shows.  The
# emulator follows the kernel beside the image (vmunix, or KERNEL): a 64-bit
# ELF kernel (an amd64 image's bios variant) runs on qemu-system-x86_64, which
# qemu-system-i386 cannot (it stops at "Booting from Hard Disk..."), and a
# 32-bit one, or an image without a kernel beside it, on qemu-system-i386.
#
# BOOT_MODE=raspi4b boots a Raspberry Pi 4 image (arm64) in QEMU's raspi4b.
# QEMU does not run the Pi's GPU firmware, so the kernel (vmunix beside the
# image, or KERNEL) and the DTB are given to it directly, and the image is the
# SD card.  The console centres its 80x25 grid on the framebuffer, which the
# screen reader also tries.
#
# BOOT_MODE=riscv64-virt boots a RISC-V kernel directly in QEMU's virt machine:
# the operand (or KERNEL, default build/riscv64-virt/vmunix) is the kernel ELF,
# which the default firmware (OpenSBI) enters in supervisor mode.  The machine
# has a ramfb display, which the kernel's console draws to, so the screen is
# read the same way.  INITRD (an initial ramdisk), BOOT_APPEND (the kernel
# command line), BOOT_SMP (default 4), BOOT_MEMORY (default 2G) and QEMU_EXTRA
# (more emulator options, split on spaces, such as -cpu rv64,sstc=off) shape
# the machine.  BOOT_MODE=riscv64-uboot boots the operand as a disk image on
# virtio-blk through U-Boot (UBOOT, default the Debian u-boot-qemu
# qemu-riscv64_smode build), with the same display.  Both run on TCG.  The
# serial line is a socket only typed into by BOOT_THEN; what the guest writes
# there is drained and not kept.  BOOT_MODE=riscv32-virt is riscv64-virt for
# a 32-bit kernel (default build/riscv32-virt/vmunix) on qemu-system-riscv32,
# with BOOT_MEMORY defaulting to 512M.
#
# It writes OUTPUT/login.png and prints its path.  The exit status is 0 only
# when the login prompt was seen.
set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
. "$root/plan/tools/guest/qemu-accel.sh"
output=${OUTPUT:-$root/build/boot-test}
boot_timeout=${BOOT_TIMEOUT:-180}
boot_mode=${BOOT_MODE:-uefi-nvme}
boot_interval=${BOOT_INTERVAL:-2}
# The riscv64 modes take a kernel or their own disk as the default operand.
case $boot_mode in
riscv64-virt)
	image=${1:-${IMAGE:-${KERNEL:-$root/build/riscv64-virt/vmunix}}}
	boot_interval=${BOOT_INTERVAL:-0.5}
	;;
riscv64-uboot)
	image=${1:-${IMAGE:-$root/build/riscv64-virt/hdd-image.img}}
	boot_interval=${BOOT_INTERVAL:-0.5}
	;;
riscv32-virt)
	image=${1:-${IMAGE:-${KERNEL:-$root/build/riscv32-virt/vmunix}}}
	boot_interval=${BOOT_INTERVAL:-0.5}
	;;
*)
	image=${1:-${IMAGE:-$root/build/amd64/hdd-image.img}}
	;;
esac
code=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
vars=${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}

case $boot_mode in
uefi-usb|uefi-nvme)
	qemu=${QEMU:-qemu-system-x86_64}
	required=("$image" "$code" "$vars")
	;;
bios-ide)
	# The ELF class of the kernel beside the image (byte 5: 1 is 32-bit, 2 is 64-bit) chooses the emulator.
	kernel=${KERNEL:-$(dirname -- "$image")/vmunix}
	bios_qemu=qemu-system-i386
	if [[ -f $kernel ]]; then
		elf_class=$(od -An -tu1 -j4 -N1 -- "$kernel" | tr -d ' ')
		if [[ $elf_class == 2 ]]; then
			bios_qemu=qemu-system-x86_64
		fi
	fi
	qemu=${QEMU:-$bios_qemu}
	required=("$image")
	;;
raspi4b)
	qemu=${QEMU:-qemu-system-aarch64}
	kernel=${KERNEL:-$(dirname -- "$image")/vmunix}
	# The build puts the device tree beside the image (ws233-p001); an image built before that has none, and the
	# old submodule's copy is the same file.
	dtb=${DTB:-$(dirname -- "$image")/bcm2711-rpi-4-b.dtb}
	if [[ -z ${DTB:-} && ! -f $dtb && -f $root/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb ]]; then
		dtb=$root/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
	fi
	required=("$image" "$kernel" "$dtb")
	;;
riscv64-virt)
	qemu=${QEMU:-qemu-system-riscv64}
	required=("$image")
	if [[ -n ${INITRD:-} ]]; then
		required+=("$INITRD")
	fi
	;;
riscv32-virt)
	qemu=${QEMU:-qemu-system-riscv32}
	required=("$image")
	if [[ -n ${INITRD:-} ]]; then
		required+=("$INITRD")
	fi
	;;
riscv64-uboot)
	qemu=${QEMU:-qemu-system-riscv64}
	uboot=${UBOOT:-/usr/lib/u-boot/qemu-riscv64_smode/u-boot.bin}
	required=("$image" "$uboot")
	;;
*)
	echo "boot-test: unknown BOOT_MODE: $boot_mode" >&2
	exit 1
	;;
esac

for file in "${required[@]}"; do
	if [[ ! -f $file ]]; then
		echo "boot-test: missing file: $file" >&2
		exit 1
	fi
done

# An earlier output is moved aside, not removed (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh).
if [[ -e $output ]]; then
	mv -- "$output" "$output.old.$$"
fi
mkdir -p "$output"

# The emulator's working files live on a scratch file system, normally the
# tmpfs /tmp, not beside the output.  The emulator writes the guest's disk and
# each screen frame from its main loop; on a full or busy disk those writes
# stall it for tens of seconds and the monitor stops answering (ws129-p009:
# QMP screendump took over 30 s with the stick in build/ and 0.02 s with it
# in /tmp, for the same image).
work_parent=${BOOT_TEST_WORK:-${TMPDIR:-/tmp}}
mkdir -p "$work_parent"
work=$(mktemp -d "$work_parent/boot-test.XXXXXX")
disk=$work/stick.qcow2
nvram=$work/uefi-vars.fd
monitor=$work/qmp.sock
serial=$work/serial.sock
frame=$work/screen.ppm
screenshot=$output/login.png

# The guest writes to the stick it booted from, so it is given a copy-on-write
# overlay of the image (small, so that a work directory left for Q1's cleanup
# costs little), and the firmware gets its own variable store.  A riscv64-virt
# kernel is booted directly and has no disk.
if [[ $boot_mode != riscv64-virt && $boot_mode != riscv32-virt ]]; then
	qemu-img create -q -f qcow2 -F raw -b "$(readlink -f -- "$image")" "$disk"
fi
if [[ $boot_mode == uefi-usb || $boot_mode == uefi-nvme ]]; then
	cp -- "$vars" "$nvram"
fi

cleanup()
{
	if [[ -n ${pid:-} ]] && kill -0 "$pid" 2>/dev/null; then
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	fi
	# The work directory stays for Q1's cleanup (plan/tools/q1-clean.sh --tmp).
}
trap cleanup EXIT INT TERM

if [[ $boot_mode == uefi-usb || $boot_mode == uefi-nvme ]]; then
	# The boot disk is a USB stick (the way the machines are tried) or an
	# NVMe drive (the way they are installed; 2026-09-25 user direction:
	# the USB model is slow and is not used for measuring).
	if [[ $boot_mode == uefi-nvme ]]; then
		boot_device=(-device nvme,serial=zedbsd-boot,drive=boot,bootindex=1)
	else
		boot_device=(-device usb-storage,bus=xhci.0,port=1,drive=boot,id=rootstick,bootindex=1)
	fi
	# amd64 is tested with 8 GiB (2026-09-24 user decision).  raspi4b
	# below cannot follow: QEMU's model of the board takes only 2 GiB.
	# KVM when the host offers it (2026-10-03 user: every test uses KVM).
	read -r -a accel <<<"$(qemu_accel_args max)"
	"$qemu" -machine q35 -m 8G -smp 4 "${accel[@]}" \
		-drive "if=pflash,format=raw,readonly=on,file=$code" \
		-drive "if=pflash,format=raw,file=$nvram" \
		-device qemu-xhci,id=xhci \
		-drive "if=none,id=boot,file=$disk,format=qcow2" \
		"${boot_device[@]}" \
		-device usb-kbd,bus=xhci.0,port=2 \
		-vga std -display none -no-reboot \
		-qmp "unix:$monitor,server,nowait" >"$output/qemu.log" 2>&1 &
elif [[ $boot_mode == riscv64-virt || $boot_mode == riscv64-uboot || $boot_mode == riscv32-virt ]]; then
	# The virt machine with a ramfb display, the console the kernel draws,
	# and its first serial line on a socket for typing into.
	riscv_memory=2G
	if [[ $boot_mode == riscv32-virt ]]; then
		riscv_memory=512M
	fi
	riscv_machine=(-machine virt -smp "${BOOT_SMP:-4}" -m "${BOOT_MEMORY:-$riscv_memory}"
		-device ramfb -display none -no-reboot
		-serial "unix:$serial,server,nowait"
		-qmp "unix:$monitor,server,nowait")
	if [[ $boot_mode == riscv64-virt || $boot_mode == riscv32-virt ]]; then
		riscv_boot=(-kernel "$image")
		if [[ -n ${INITRD:-} ]]; then
			riscv_boot+=(-initrd "$INITRD")
		fi
		if [[ -n ${BOOT_APPEND:-} ]]; then
			riscv_boot+=(-append "$BOOT_APPEND")
		fi
	else
		riscv_boot=(-kernel "$uboot"
			-drive "if=none,id=boot,file=$disk,format=qcow2"
			-device virtio-blk-device,drive=boot)
	fi
	read -r -a riscv_extra <<<"${QEMU_EXTRA:-}"
	"$qemu" "${riscv_machine[@]}" "${riscv_boot[@]}" "${riscv_extra[@]}" \
		>"$output/qemu.log" 2>&1 &
elif [[ $boot_mode == raspi4b ]]; then
	"$qemu" -machine raspi4b -m 2G -kernel "$kernel" -dtb "$dtb" \
		-drive "if=sd,format=qcow2,file=$disk" \
		-display none -serial null -serial null -no-reboot \
		-qmp "unix:$monitor,server,nowait" >"$output/qemu.log" 2>&1 &
else
	"$qemu" -machine pc -m 256 -cpu max \
		-drive "if=ide,bus=0,unit=0,format=qcow2,file=$disk" \
		-vga std -display none -no-reboot \
		-qmp "unix:$monitor,server,nowait" >"$output/qemu.log" 2>&1 &
fi
pid=$!

# The screen is read, not a character stream: the login prompt is the string
# the console draws, so the picture is compared against what OCR would have to
# read.  Instead of reading pixels, this waits for the guest to stop writing to
# the stick and then photographs the screen; the login prompt is the first
# quiet point after the boot, and the picture shows whether that is what it is.
watch=(--monitor "$monitor" --screenshot "$screenshot" --frame "$frame"
	--timeout "$boot_timeout" --interval "$boot_interval")
if [[ -n ${BOOT_PATTERN:-} ]]; then
	while IFS= read -r line; do
		if [[ -n $line ]]; then
			watch+=(--pattern "$line")
		fi
	done <<<"$BOOT_PATTERN"
fi
if [[ -n ${BOOT_THEN:-} ]]; then
	watch+=(--serial "$serial")
	while IFS= read -r line; do
		case $line in
		type:*) watch+=(--then-type "${line#type:}") ;;
		expect:*) watch+=(--then-expect "${line#expect:}") ;;
		'') ;;
		*)
			echo "boot-test: unknown BOOT_THEN step: $line" >&2
			exit 1
			;;
		esac
	done <<<"$BOOT_THEN"
fi
status=0
python3 "$root/plan/tools/boot-test.py" "${watch[@]}" || status=$?

if [[ $status -eq 0 ]]; then
	echo "boot-test: PASS $screenshot"
fi
exit $status
