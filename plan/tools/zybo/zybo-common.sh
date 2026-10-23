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
