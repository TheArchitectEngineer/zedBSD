#!/bin/sh
# Writes a bitstream into the ZYBO's PL over JTAG with openFPGALoader
# (design §4.5), without touching the PS: SPL or a kernel may keep running.
# The PL's level shifters and FCLK resets are not changed here; open them
# afterwards (OpenOCD's zybo_pl_enable, or fpgaload --enable under zedBSD).
#   plan/tools/zybo/pl-jtag.sh FILE.bit
# The board's FT2232H is the same one OpenOCD uses, so the host's board lock
# is taken first.  ZYBO_PL_INDEX overrides the PL's place in the JTAG chain
# (default 1, the order of openFPGALoader --detect: the ARM DAP is 0).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/zybo-common.sh"
[ $# -eq 1 ] && [ -f "$1" ] || { echo "usage: pl-jtag.sh FILE.bit" >&2; exit 2; }
zybo_lock
start=$(date +%s)
timeout "${ZYBO_PL_TIMEOUT:-120}" openFPGALoader -c digilent \
	--ftdi-serial 210279785182 --index-chain "${ZYBO_PL_INDEX:-1}" \
	--write-sram "$1"
rc=$?
echo "pl-jtag: rc $rc in $(( $(date +%s) - start )) s, $(sha256sum "$1" | cut -c1-16) $1"
exit $rc
