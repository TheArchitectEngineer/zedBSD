# Sourced by the KC705 board scripts (jtag-load.sh, jtag-read.sh).  The host
# has two Digilent FT2232H cables (the ZYBO's is 210279785182), so every tool
# names the KC705's by its serial, and every script that drives its JTAG first
# takes the host's lock /tmp/zedbsd-kc705-board.lock (never removed); a script
# that calls another passes the lock on through KC705_LOCK_HELD.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
kc705_dir=$(cd "$(dirname -- "$0")" && pwd)
kc705_root=$(cd "$kc705_dir/../../.." && pwd)
kc705_cfg=$kc705_dir/openocd-kc705.cfg
kc705_serial=210203368704
kc705_lock_file=/tmp/zedbsd-kc705-board.lock

kc705_lock() {
	[ -n "${KC705_LOCK_HELD:-}" ] && return 0
	exec 8>>"$kc705_lock_file"
	if ! flock -w "${KC705_LOCK_WAIT:-900}" 8; then
		echo "kc705: the board is busy (lock $kc705_lock_file)" >&2
		exit 1
	fi
	KC705_LOCK_HELD=$$
	export KC705_LOCK_HELD
}

# Runs one OpenOCD session with the board's configuration; the commands are
# its argument, e.g. kc705_openocd 'init; shutdown'.
kc705_openocd() {
	timeout "${KC705_OPENOCD_TIMEOUT:-120}" openocd -f "$kc705_cfg" -c "$1" 2>&1
}
