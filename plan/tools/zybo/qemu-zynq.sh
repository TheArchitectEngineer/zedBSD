#!/bin/sh
# Boots a zybo kernel (build/zybo/vmunix, the ELF) in the emulator's
# xilinx-zynq-a9 machine the way the JTAG boot runs it on the board
# (design §4.6, §7.3): the ELF loaded at its physical addresses and entered
# secure at e_entry, the same DTB as jtag-boot.sh at 0x01F00000 and the root
# image at 0x04000000, both through -device loader (the ELF loader passes
# nothing in r2; the kernel then takes the tree at 0x01F00000).  UART1, the
# console, is the second serial port, a Unix socket.
#   plan/tools/zybo/qemu-zynq.sh KERNEL [--append TEXT] [--initrd root.img]
#       [--sd zybo-sd.img] [--net PORT] [--expect REGEX]... [--then STEP]...
#       [--timeout S] [--stamp] [--gdb PORT] [--print]
# --sd puts a disk image in the SD slot of SDIO0 (make sd-image,
# build/zybo/zybo-sd.img, a power of two in size); the machine writes to
# it.  With --sd and no --initrd, --append 'rootpart=/dev/mmcblk0p2' takes
# the root from the card.
# --net PORT gives GEM0, the first Ethernet controller, the emulator's user
# network (the guest's DHCP lease is 10.0.2.15) and forwards the host's
# 127.0.0.1:PORT to the guest's SSH port (the WS289 U4 exception: the LAN
# is judged by SSH from the host); a --then run: step can ssh in while the
# line is watched.
# --expect waits on the live serial line for each REGEX and gives the exit
# status (the U3 exception: nothing is kept in a file); --stamp puts the
# host's seconds before each line, for the TIMER 0S/10S check; --then holds
# a dialogue on the line once every REGEX has matched (uart-expect.py:
# type:TEXT, expect:REGEX, run:COMMAND), such as a login.  Without
# --expect the machine runs until --timeout and the socket can be attached
# to (plan/tools/guest/serial.py --socket).  --gdb PORT starts it stopped
# with a gdbstub on 127.0.0.1:PORT.  --print shows the command and stops.
# ZYBO_QEMU_OUT is the directory for the DTB and the socket (default
# build/zybo-qemu), QEMU the emulator, QEMU_EXTRA more options.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/zybo-common.sh"

dtb_addr=0x01f00000
initrd_addr=0x04000000
kernel= append= initrd= sd= net_port= timeout=60 stamp= gdb_port= print_only=
patterns= steps=
while [ $# -gt 0 ]; do
	case "$1" in
	--append) append=$2; shift 2 ;;
	--initrd) initrd=$2; shift 2 ;;
	--sd) sd=$2; shift 2 ;;
	--net) net_port=$2; shift 2 ;;
	--expect) patterns="$patterns$2
"; shift 2 ;;
	--then) steps="$steps$2
"; shift 2 ;;
	--timeout) timeout=$2; shift 2 ;;
	--stamp) stamp=--stamp; shift ;;
	--gdb) gdb_port=$2; shift 2 ;;
	--print) print_only=1; shift ;;
	-*) echo "qemu-zynq: unknown option $1" >&2; exit 2 ;;
	*) kernel=$1; shift ;;
	esac
done
if [ -z "$kernel" ]; then
	echo "usage: qemu-zynq.sh KERNEL(ELF) [options]" >&2
	exit 2
fi
if [ "$(head -c 4 "$kernel" | od -An -tx1 | tr -d ' ')" != 7f454c46 ]; then
	echo "qemu-zynq: $kernel is not an ELF (give build/zybo/vmunix, not vmunix.bin)" >&2
	exit 2
fi

out=${ZYBO_QEMU_OUT:-$zybo_root/build/zybo-qemu}
sock=$out/uart1.sock

# The same tree the JTAG boot gives the board.
if [ -n "$initrd" ]; then
	initrd_end=$(printf '0x%08x' $((initrd_addr + $(wc -c < "$initrd"))))
	zybo_make_dtb "$out" "$append" "$initrd_addr" "$initrd_end" || exit 1
else
	zybo_make_dtb "$out" "$append" || exit 1
fi
dtb=$out/zybo.dtb

set -- -M xilinx-zynq-a9 -smp 1 -m 512M -kernel "$kernel" \
	-serial null -serial "unix:$sock,server=on,wait=off" \
	-device "loader,file=$dtb,addr=$dtb_addr,force-raw=on"
[ -n "$initrd" ] && set -- "$@" -device "loader,file=$initrd,addr=$initrd_addr,force-raw=on"
[ -n "$sd" ] && set -- "$@" -drive "if=sd,index=0,file=$sd,format=raw"
[ -n "$net_port" ] && set -- "$@" -nic "user,model=cadence_gem,hostfwd=tcp:127.0.0.1:$net_port-:22"
set -- "$@" -display none -monitor none -no-reboot
[ -n "$gdb_port" ] && set -- "$@" -S -gdb "tcp:127.0.0.1:$gdb_port"
# shellcheck disable=SC2086
set -- "$@" ${QEMU_EXTRA:-}
qemu=${QEMU:-qemu-system-arm}

if [ -n "$print_only" ]; then
	printf '%s' "$qemu"
	printf ' %s' "$@"
	printf '\n'
	exit 0
fi

sha256sum "$kernel" "$dtb" ${initrd:+"$initrd"}
timeout "$((timeout + 10))" "$qemu" "$@" &
qemu_pid=$!

status=0
if [ -n "$patterns" ]; then
	# The watcher connects as soon as the socket is there and judges the live lines.
	old_ifs=$IFS
	IFS='
'
	set --
	for pattern in $patterns; do set -- "$@" "$pattern"; done
	for step in $steps; do set -- "$@" --then "$step"; done
	IFS=$old_ifs
	python3 -I -B "$zybo_dir/uart-expect.py" --socket "$sock" --timeout "$timeout" $stamp "$@"
	status=$?
	kill "$qemu_pid" 2>/dev/null
else
	echo "qemu-zynq: running; UART1 on $sock" >&2
fi
wait "$qemu_pid" 2>/dev/null
exit "$status"
