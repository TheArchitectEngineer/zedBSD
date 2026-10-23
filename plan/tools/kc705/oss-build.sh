#!/bin/sh
# Builds a bitstream for the KC705 (xc7k325tffg900-2) with openXC7: yosys
# synth_xilinx, nextpnr-himbaechel (xilinx), and fpga-as for the frames and
# the .bit (the same flow as plan/tools/zybo/pl-build.sh).  Outside zedBSD's
# make; the output stays under BUILD.
#   plan/tools/kc705/oss-build.sh RTL_DIR [BUILD]   (default build/kc705/oss)
# RTL_DIR holds NAME.v (top module NAME, the directory's name) and NAME.xdc,
# e.g. platform/fpga/kc705/bringup.  Output: BUILD/NAME/NAME.bit (from
# NAME.fasm, nextpnr's NAME.raw.fasm after fasm-hp-input-pair.awk) and the
# logs BUILD/NAME/yosys.log, nextpnr.log (utilisation and Fmax).  The chip
# database is made once into BUILD/chipdb/xc7k325t.bin.  OPENXC7 is the
# installation (default $HOME/opt/openxc7; ws286 p003).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
[ $# -ge 1 ] && [ -d "$1" ] || { echo "usage: oss-build.sh RTL_DIR [BUILD]" >&2; exit 2; }
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
rtl=$(cd "$1" && pwd)
name=$(basename "$rtl")
build=${2:-$root/build/kc705/oss}
openxc7=${OPENXC7:-$HOME/opt/openxc7}
part=xc7k325tffg900-2
die=xc7k325t
family=kintex7
[ -f "$openxc7/export.sh" ] || { echo "oss-build: no openXC7 at $openxc7" >&2; exit 1; }
. "$openxc7/export.sh"
[ -f "$rtl/$name.v" ] && [ -f "$rtl/$name.xdc" ] || {
	echo "oss-build: $rtl needs $name.v and $name.xdc" >&2; exit 1; }
mkdir -p "$build/chipdb" "$build/$name"
out=$build/$name/$name
chipdb=$build/chipdb/$die.bin

if [ ! -f "$chipdb" ]; then
	echo "oss-build: making the chip database for $die"
	python3 "$openxc7/share/nextpnr/himbaechel/uarch/xilinx/gen/xilinx_gen.py" \
		--xray "$PRJXRAY_DB_DIR/$family" --device "$die" --bba "$build/chipdb/$die.bba"
	bbasm -l "$build/chipdb/$die.bba" "$chipdb.tmp"
	mv "$chipdb.tmp" "$chipdb"
fi

yosys -q -l "$build/$name/yosys.log" \
	-p "synth_xilinx -flatten -abc9 -arch xc7 -top $name; write_json $out.json" \
	"$rtl"/*.v
nextpnr-himbaechel --chipdb "$chipdb" --device "$part" --json "$out.json" \
	-o xdc="$rtl/$name.xdc" -o fasm="$out.raw.fasm" \
	--log "$build/$name/nextpnr.log" ${PNR_ARGS:-} >/dev/null 2>"$build/$name/nextpnr.err" || {
	tail -20 "$build/$name/nextpnr.log" "$build/$name/nextpnr.err" >&2; exit 1; }
# openXC7's input pairs on the HP banks need fixing (fasm-hp-input-pair.awk).
awk -f "$root/plan/tools/kc705/fasm-hp-input-pair.awk" "$out.raw.fasm" "$out.raw.fasm" > "$out.fasm"
# fpga-as writes to its standard output: keep a failed run's partial file
# away from the name openFPGALoader is given.
fpga-as --prjxray_db_path="$PRJXRAY_DB_DIR/$family" --part "$part" "$out.fasm" > "$out.bit.tmp"
mv "$out.bit.tmp" "$out.bit"

awk '/Device utilisation/ { u = 1; next }
	u && !/^Info:[ \t]+[A-Za-z0-9_]+: +[0-9]+\/ *[0-9]+/ { u = 0 }
	u && !/ 0\// { print }' "$build/$name/nextpnr.log"
grep 'Max frequency for clock' "$build/$name/nextpnr.log" | tail -2 || true
grep -c -i 'warning' "$build/$name/nextpnr.log" | sed 's/^/oss-build: nextpnr warning lines /'
echo "oss-build: $out.bit $(wc -c < "$out.bit") bytes, sha256 $(sha256sum "$out.bit" | cut -c1-16)"
