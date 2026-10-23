#!/bin/sh
# Boots the ZYBO over JTAG (design §4.1): resets the PS, runs U-Boot SPL from
# OCM to set up the clocks, MIO, DDR and UART1, stops it in board_boot_order,
# loads a kernel (and a DTB and a root image) into DDR, checks them by reading
# them back, and enters the kernel with r2 = the DTB.  The boot-mode jumper
# must be on JTAG; nothing is loaded otherwise.
#   plan/tools/zybo/jtag-boot.sh KERNEL [--dtb FILE] [--append TEXT]
#       [--initrd root.img] [--no-reset] [--expect REGEX]... [--timeout S]
#       [--stamp]
#   plan/tools/zybo/jtag-boot.sh --spl-only [--expect REGEX]...
# --spl-only stops with SPL halted (DDR up, the PL still off): for DDR and PL
# tests over JTAG.  --no-reset skips the reset and SPL and loads into a DDR
# that an earlier run set up (such as --spl-only).  --expect waits on the live UART for each REGEX (SPL's
# banner is always one) and gives the exit status; nothing is kept in a file.
# --stamp puts the host's seconds before each UART line (the TIMER 0S/10S check).
# KERNEL is a raw image entered at its first byte, 0x00100000, or an ELF:
# OpenOCD then loads each PT_LOAD at its physical address (BSS is not zeroed;
# the kernel clears its own) and it is entered at e_entry's physical address.
# Either way it must lie within 0x00100000 and the DTB, BSS included: for a
# raw image the ELF of the same name without .bin, when there is one, gives
# where its BSS ends.
# Without --dtb a DTB is made here: memory@0 512 MiB, /chosen bootargs from
# --append, and the initrd's start and end.  Memory map: kernel 0x00100000,
# DTB 0x01F00000, initrd 0x04000000.
# ZYBO_SPL picks another SPL (default build/zybo-spl/out/spl/u-boot-spl.bin
# from spl-build.sh, with its ELF beside it); ZYBO_BOOT_OUT the directory for
# the made DTB (default build/zybo-boot); JTAG_KHZ the JTAG clock.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/zybo-common.sh"

kernel_addr=0x00100000
dtb_addr=0x01F00000
initrd_addr=0x04000000
kernel= dtb= append= initrd= reset=1 spl_only= timeout=60 stamp=
patterns=
while [ $# -gt 0 ]; do
	case "$1" in
	--dtb) dtb=$2; shift 2 ;;
	--append) append=$2; shift 2 ;;
	--initrd) initrd=$2; shift 2 ;;
	--no-reset) reset=; shift ;;
	--spl-only) spl_only=1; shift ;;
	--expect) patterns="$patterns$2
"; shift 2 ;;
	--timeout) timeout=$2; shift 2 ;;
	--stamp) stamp=--stamp; shift ;;
	-*) echo "jtag-boot: unknown option $1" >&2; exit 2 ;;
	*) kernel=$1; shift ;;
	esac
done
if [ -z "$spl_only" ] && [ -z "$kernel" ]; then
	echo "usage: jtag-boot.sh KERNEL.bin [options] | --spl-only" >&2
	exit 2
fi

spl=${ZYBO_SPL:-$zybo_root/build/zybo-spl/out/spl/u-boot-spl.bin}
spl_elf=${spl%.bin}
[ -f "$spl" ] && [ -f "$spl_elf" ] || { echo "jtag-boot: no SPL at $spl (run spl-build.sh)" >&2; exit 1; }
bp=$(${NM:-arm-linux-gnueabihf-nm} "$spl_elf" | awk '$3 == "board_boot_order" { print "0x" $1 }')
[ -n "$bp" ] || { echo "jtag-boot: no board_boot_order in $spl_elf" >&2; exit 1; }

zybo_lock

boot_out=${ZYBO_BOOT_OUT:-$zybo_root/build/zybo-boot}
entry=$kernel_addr
kernel_type=bin
if [ -z "$spl_only" ]; then
	mkdir -p "$boot_out"
	if [ "$(head -c 4 "$kernel" | od -An -tx1 | tr -d ' ')" = 7f454c46 ]; then
		kernel_type=elf
		e_entry=$(${READELF:-arm-linux-gnueabihf-readelf} -h "$kernel" | awk '/Entry point/ { print $4 }')
		low= high=0 entry=
		for segment in $(${READELF:-arm-linux-gnueabihf-readelf} -lW "$kernel" |
			awk '$1 == "LOAD" { print $3 ":" $4 ":" $6 }'); do
			vaddr=$((${segment%%:*})) rest=${segment#*:}
			start=$((${rest%%:*})) end=$((${rest%%:*} + ${rest#*:}))
			{ [ -z "$low" ] || [ "$start" -lt "$low" ]; } && low=$start
			[ "$end" -gt "$high" ] && high=$end
			# e_entry is virtual; enter at its physical address.
			if [ $((e_entry)) -ge "$vaddr" ] && [ $((e_entry)) -lt $((vaddr + end - start)) ]; then
				entry=$(printf '0x%08x' $((e_entry - vaddr + start)))
			fi
		done
		[ -n "$entry" ] || { echo "jtag-boot: e_entry $e_entry is in no PT_LOAD" >&2; exit 1; }
		kernel_addr=$(printf '0x%08x' "$low") kernel_end=$high
	else
		kernel_end=$((kernel_addr + $(wc -c < "$kernel")))
		# The raw image holds no BSS; its ELF says where the BSS ends.
		kernel_elf=${kernel%.bin}
		if [ "$kernel_elf" != "$kernel" ] && [ -f "$kernel_elf" ] &&
			[ "$(head -c 4 "$kernel_elf" | od -An -tx1 | tr -d ' ')" = 7f454c46 ]; then
			elf_end=$(zybo_elf_phys_end "$kernel_elf")
			[ $((elf_end)) -gt "$kernel_end" ] && kernel_end=$((elf_end))
		fi
	fi
	if [ $((kernel_end)) -gt $((dtb_addr)) ] || [ $((kernel_addr)) -lt $((0x00100000)) ]; then
		printf 'jtag-boot: the kernel (0x%08x-0x%08x) is not within 0x00100000-%s\n' \
			$((kernel_addr)) $((kernel_end)) "$dtb_addr" >&2
		exit 1
	fi
	if [ -n "$initrd" ] && [ $((initrd_addr + $(wc -c < "$initrd"))) -gt $((0x1FC00000)) ]; then
		echo "jtag-boot: the initrd does not fit below 0x1FC00000" >&2
		exit 1
	fi
fi

# The DTB, when the caller does not bring one.
if [ -z "$spl_only" ] && [ -z "$dtb" ]; then
	if [ -n "$initrd" ]; then
		initrd_end=$(printf '0x%08x' $((initrd_addr + $(wc -c < "$initrd"))))
		zybo_make_dtb "$boot_out" "$append" "$initrd_addr" "$initrd_end" || exit 1
	else
		zybo_make_dtb "$boot_out" "$append" || exit 1
	fi
	dtb=$boot_out/zybo.dtb
fi

if [ -n "$reset" ]; then
	"$zybo_dir/jtag-reset.sh" || exit 1
fi

# The UART watcher starts before SPL runs, so that its banner is not missed;
# it prints the live lines itself and its status is the boot's.
watcher=
if [ -n "$reset" ] || [ -n "$patterns" ]; then
	set -- 'U-Boot SPL 2025\.07'
	[ -z "$reset" ] && set --
	old_ifs=$IFS
	IFS='
'
	for pattern in $patterns; do set -- "$@" "$pattern"; done
	IFS=$old_ifs
	zybo_with_uart python3 -I "$zybo_dir/uart-expect.py" --device "$zybo_uart" \
		--timeout "$timeout" $stamp "$@" &
	watcher=$!
	sleep 1
fi

commands="init; zybo_check_jtag_mode"
[ -n "$reset" ] && commands="$commands; zybo_spl $spl $bp"
if [ -z "$spl_only" ]; then
	sha256sum "$kernel" "$dtb" ${initrd:+"$initrd"}
	if [ "$kernel_type" = elf ]; then
		commands="$commands; halt; zybo_load_elf $kernel"
	else
		commands="$commands; halt; zybo_load $kernel_addr $kernel"
	fi
	commands="$commands; zybo_load $dtb_addr $dtb"
	[ -n "$initrd" ] && commands="$commands; zybo_load $initrd_addr $initrd"
	commands="$commands; zybo_enter $entry $dtb_addr"
fi
commands="$commands; shutdown"
out=$(zybo_openocd "$commands")
status=$?
printf '%s\n' "$out" | grep -E '^zybo:|Error' | grep -v -E 'Invalid ACK'
case "$out" in
*"zybo: entered"*|*"zybo: SPL halted"*) ;;
*) status=1 ;;
esac
[ "$status" -eq 0 ] || echo "jtag-boot: OpenOCD failed" >&2

if [ -n "$watcher" ]; then
	if [ "$status" -ne 0 ]; then
		kill "$watcher" 2>/dev/null
	fi
	wait "$watcher"
	uart_status=$?
	sleep 0.3
	[ "$status" -eq 0 ] && status=$uart_status
fi
exit "$status"
