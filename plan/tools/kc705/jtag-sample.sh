#!/bin/sh
# Reads the KC705's input pads by boundary scan (SAMPLE, IR 0x01) without
# disturbing the configured design, and prints the level of each named pin,
# so that a pin the fabric reads wrong can be told from a pin that really
# is at that level.  Cell numbers come from the BSDL that Vivado ships,
# ids_lite/ISE/kintex7/data/xc7k325t_ffg900.bsd (BOUNDARY_LENGTH 1631;
# cell 0 is shifted out first).
#   plan/tools/kc705/jtag-sample.sh [BSDL] PIN...   e.g. AB12 AA12
# BSDL defaults to the file under $HOME/opt/amd/2026.1/Vivado.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/kc705-common.sh"
bsdl=$HOME/opt/amd/2026.1/Vivado/ids_lite/ISE/kintex7/data/xc7k325t_ffg900.bsd
case ${1:-} in *.bsd) bsdl=$1; shift ;; esac
[ $# -ge 1 ] && [ -f "$bsdl" ] || { echo "usage: jtag-sample.sh [BSDL] PIN..." >&2; exit 2; }
len=$(sed -n 's/.*BOUNDARY_LENGTH of .* is \([0-9]*\);.*/\1/p' "$bsdl")
[ -n "$len" ] || { echo "jtag-sample: no BOUNDARY_LENGTH in $bsdl" >&2; exit 1; }
# drscan takes fields of at most 32 bits.
fields= n=$len
while [ "$n" -gt 32 ]; do fields="$fields 32 0"; n=$((n - 32)); done
fields="$fields $n 0"
kc705_lock
log=$(kc705_openocd "init; irscan kc705.fpga 0x01;
	echo \"bsr [drscan kc705.fpga$fields]\"; irscan kc705.fpga 0x3f; shutdown")
rc=$?
bsr=$(printf '%s\n' "$log" | sed -n 's/^bsr //p')
if [ $rc -ne 0 ] || [ -z "$bsr" ]; then
	printf '%s\n' "$log" | tail -15 >&2
	echo "jtag-sample: FAIL (openocd rc $rc)"; exit 1
fi
for pin in "$@"; do
	cell=$(sed -n "s/^[^0-9]*\([0-9][0-9]*\) (BC_[0-9]*, IO_$pin, input,.*/\1/p" "$bsdl")
	[ -n "$cell" ] || { echo "jtag-sample: $pin: no input cell"; continue; }
	set -- $bsr
	shift $((cell / 32))
	echo "jtag-sample: $pin (cell $cell) $(( (0x$1 >> (cell % 32)) & 1 ))"
done
