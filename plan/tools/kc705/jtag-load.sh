#!/bin/sh
# Writes a bitstream into the KC705's FPGA (SRAM, lost at power-off) over the
# onboard Digilent USB JTAG with openFPGALoader, under the host's board lock.
#   plan/tools/kc705/jtag-load.sh FILE.bit
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/kc705-common.sh"
[ $# -eq 1 ] && [ -f "$1" ] || { echo "usage: jtag-load.sh FILE.bit" >&2; exit 2; }
kc705_lock
start=$(date +%s)
timeout "${KC705_LOAD_TIMEOUT:-180}" openFPGALoader -c digilent \
	--ftdi-serial "$kc705_serial" --index-chain 0 --write-sram "$1"
rc=$?
echo "jtag-load: rc $rc in $(( $(date +%s) - start )) s, $(sha256sum "$1" | cut -c1-16) $1"
exit $rc
