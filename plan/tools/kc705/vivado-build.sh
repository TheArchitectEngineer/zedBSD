#!/bin/bash
# Builds a bitstream for the KC705 (xc7k325tffg900-2) with Vivado in
# non-project batch mode (vivado-build.tcl).  Outside zedBSD's make; the
# output stays under BUILD, never in the tree.
#   plan/tools/kc705/vivado-build.sh RTL_DIR [BUILD]   (default build/kc705/vivado)
# RTL_DIR holds NAME.v (top module NAME, the directory's name) and NAME.xdc,
# e.g. platform/fpga/kc705/bringup.  Output: BUILD/NAME/NAME.bit, the reports
# and BUILD/NAME/vivado.log.  VIVADO is the installation's settings64.sh
# (default $HOME/opt/amd/2026.1/Vivado/settings64.sh); PART overrides the
# part.  bash, because settings64.sh is bash.  Prints the utilisation's main
# rows, WNS/WHS and the license lines.  Vivado 2026.1 does not start without
# a license file (ws288 phase001).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
[ $# -ge 1 ] && [ -d "$1" ] || { echo "usage: vivado-build.sh RTL_DIR [BUILD]" >&2; exit 2; }
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
tcl=$root/plan/tools/kc705/vivado-build.tcl
rtl=$(cd "$1" && pwd)
name=$(basename "$rtl")
build=${2:-$root/build/kc705/vivado}
settings=${VIVADO:-$HOME/opt/amd/2026.1/Vivado/settings64.sh}
part=${PART:-xc7k325tffg900-2}
[ -f "$settings" ] || { echo "vivado-build: no Vivado at $settings" >&2; exit 1; }
. "$settings"
out=$build/$name
mkdir -p "$out"
start=$(date +%s)
# Vivado writes its .Xil scratch into the current directory.
rc=0
(cd "$out" && timeout "${VIVADO_TIMEOUT:-1800}" vivado -mode batch -nojournal \
	-log vivado.log -source "$tcl" -tclargs "$rtl" "$out" "$part" >/dev/null) || rc=$?
if [ $rc -ne 0 ]; then
	grep -E '^(ERROR|CRITICAL WARNING)' "$out/vivado.log" | head -20 >&2
	echo "vivado-build: failed rc $rc (log $out/vivado.log)" >&2
	exit 1
fi
grep -i -E 'license' "$out/vivado.log" | sort -u | head -6
grep -E '^\| (Slice LUTs|Slice Registers|Block RAM Tile|DSPs|Bonded IOB|BUFGCTRL) ' \
	"$out/utilization.rpt" | head -8
grep -E '^vivado-build:' "$out/vivado.log" | tail -1
echo "vivado-build: $(grep -c '^WARNING' "$out/vivado.log") warnings, $(grep -c '^CRITICAL WARNING' "$out/vivado.log") critical, $(( $(date +%s) - start )) s"
echo "vivado-build: $out/$name.bit $(wc -c < "$out/$name.bit") bytes, sha256 $(sha256sum "$out/$name.bit" | cut -c1-16)"
