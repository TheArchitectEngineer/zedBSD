# Sourced by the ZYBO board scripts (jtag-reset.sh, jtag-probe.sh, jtag-boot.sh).
# The board is one, so every script that drives its JTAG or UART first takes
# the host's lock /tmp/zedbsd-zybo-board.lock (never removed); a script that
# calls another passes the lock on through ZYBO_LOCK_HELD.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
zybo_dir=$(cd "$(dirname -- "$0")" && pwd)
zybo_root=$(cd "$zybo_dir/../../.." && pwd)
zybo_cfg=$zybo_dir/openocd-zybo.cfg
zybo_uart=${ZYBO_UART:-/dev/serial/by-id/usb-Digilent_Digilent_Adept_USB_Device_210279785182-if01-port0}
zybo_lock_file=/tmp/zedbsd-zybo-board.lock

zybo_lock() {
	[ -n "${ZYBO_LOCK_HELD:-}" ] && return 0
	exec 9>>"$zybo_lock_file"
	if ! flock -w "${ZYBO_LOCK_WAIT:-900}" 9; then
		echo "zybo: the board is busy (lock $zybo_lock_file)" >&2
		exit 1
	fi
	ZYBO_LOCK_HELD=$$
	export ZYBO_LOCK_HELD
}

# Runs one OpenOCD session with the board's configuration; the commands are
# its arguments, e.g. zybo_openocd 'init; halt; shutdown'.  The servers are
# off, so nothing else can attach in the middle.
zybo_openocd() {
	timeout "${ZYBO_OPENOCD_TIMEOUT:-300}" openocd -f "$zybo_cfg" -c "$1" 2>&1
}

# Runs a command with the UART's group when this login does not have it yet.
zybo_with_uart() {
	if [ -r "$zybo_uart" ] && [ -w "$zybo_uart" ]; then
		"$@"
	else
		zybo_command=
		for zybo_arg in "$@"; do
			zybo_quoted=$(printf '%s' "$zybo_arg" | sed "s/'/'\\\\''/g")
			zybo_command="$zybo_command '$zybo_quoted'"
		done
		sg dialout -c "$zybo_command"
	fi
}

# Writes DIR/zybo.dtb: memory@0 512 MiB and /chosen with the command line
# and, when START is not empty, the initrd from START to END.  The JTAG boot
# and the emulator's script (qemu-zynq.sh) give the kernel the same tree.
#   zybo_make_dtb DIR APPEND [START END]
zybo_make_dtb() {
	zybo_dtb_dir=$1 zybo_dtb_append=$2
	zybo_dtb_initrd=
	if [ -n "${3:-}" ]; then
		zybo_dtb_initrd="linux,initrd-start = <$3>; linux,initrd-end = <$4>;"
	fi
	zybo_dtb_bootargs=$(printf '%s' "$zybo_dtb_append" | sed 's/\\/\\\\/g; s/"/\\"/g')
	mkdir -p "$zybo_dtb_dir" || return 1
	cat > "$zybo_dtb_dir/zybo.dts" <<EOF_DTS
/dts-v1/;
/ {
	#address-cells = <1>;
	#size-cells = <1>;
	model = "Digilent ZYBO (JTAG boot)";
	compatible = "digilent,zynq-zybo", "xlnx,zynq-7000";
	memory@0 { device_type = "memory"; reg = <0x0 0x20000000>; };
	chosen { bootargs = "$zybo_dtb_bootargs"; stdout-path = "serial0:115200n8"; $zybo_dtb_initrd };
};
EOF_DTS
	dtc -q -I dts -O dtb -o "$zybo_dtb_dir/zybo.dtb" "$zybo_dtb_dir/zybo.dts"
}

# Prints the physical end (past the last byte, BSS included) of an ELF's
# PT_LOAD segments, as 0x%08x.
#   zybo_elf_phys_end FILE
zybo_elf_phys_end() {
	${READELF:-arm-linux-gnueabihf-readelf} -lW "$1" |
		awk '$1 == "LOAD" { end = strtonum($4) + strtonum($6); if (end > high) high = end }
		     END { printf "0x%08x\n", high }'
}
