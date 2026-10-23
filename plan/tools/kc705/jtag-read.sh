#!/bin/sh
# Reads platform/fpga/kc705/bringup's BSCANE2 register (USER1, IR 0x02, 128
# bits; layout in bringup.v) twice, 100 ms apart, with OpenOCD, and prints
# the fields and the counter's rate.  With --write VALUE it first writes
# SCRATCH (VALUE with bit 31 set shows VALUE[7:0] on the LEDs; 0 gives the
# LEDs back to the pattern).  Exit 0 when the ID matches and the counter
# advanced.
#   plan/tools/kc705/jtag-read.sh [--write VALUE]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/kc705-common.sh"
write=
if [ $# -eq 2 ] && [ "$1" = --write ]; then
	write="irscan kc705.fpga 0x02; drscan kc705.fpga 32 $2 32 0x57524954 32 0 32 0;"
elif [ $# -ne 0 ]; then
	echo "usage: jtag-read.sh [--write VALUE]" >&2; exit 2
fi
kc705_lock
log=$(kc705_openocd "init; $write
	irscan kc705.fpga 0x02; echo \"dr0 [drscan kc705.fpga 32 0 32 0 32 0 32 0]\"
	sleep 100
	irscan kc705.fpga 0x02; echo \"dr1 [drscan kc705.fpga 32 0 32 0 32 0 32 0]\"
	shutdown")
rc=$?
r0=$(printf '%s\n' "$log" | sed -n 's/^dr0 //p')
r1=$(printf '%s\n' "$log" | sed -n 's/^dr1 //p')
if [ $rc -ne 0 ] || [ -z "$r0" ] || [ -z "$r1" ]; then
	printf '%s\n' "$log" | tail -15 >&2
	echo "jtag-read: FAIL (openocd rc $rc)"; exit 1
fi
gray2bin() {
	g=$((0x$1)); b=$g; s=1
	while [ $s -lt 32 ]; do b=$(( b ^ (b >> s) )); s=$((s * 2)); done
	echo $(( b & 0xffffffff ))
}
set -- $r0; id0=$1; st0=$2; c0=$(gray2bin "$3"); sc0=$4
set -- $r1; id1=$1; st1=$2; c1=$(gray2bin "$3"); sc1=$4
st=$((0x$st1))
printf 'jtag-read: ID %s STATUS %s (VERSION %d, LED 0x%02x, buttons C,W,S,E,N 0x%02x, DIP 0x%x) SCRATCH %s\n' \
	"$id1" "$st1" $(( st >> 24 )) $(( (st >> 16) & 0xff )) $(( (st >> 4) & 0x1f )) $(( st & 0xf )) "$sc1"
d=$(( (c1 - c0) & 0xffffffff ))
echo "jtag-read: COUNTER $c0 -> $c1 (+$d in about 100 ms of OpenOCD sleep)"
if [ "$id0" = 4b373035 ] && [ "$id1" = 4b373035 ] && [ $d -gt 0 ]; then
	echo "jtag-read: PASS"
else
	echo "jtag-read: FAIL"; exit 1
fi
