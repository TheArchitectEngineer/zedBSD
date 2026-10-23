#!/bin/sh
# Builds a PL bitstream for the original ZYBO (xc7z010clg400-1) with openXC7
# (design §5.10): yosys synth_xilinx, nextpnr-himbaechel (xilinx), and
# fpga-as for the frames and the .bit.  Outside zedBSD's make.
#   plan/tools/zybo/pl-build.sh RTL_DIR [BUILD]   (default build/zybo-pl)
# RTL_DIR holds NAME.v (top module NAME, the directory's name) and NAME.xdc,
# e.g. platform/armv7/zybo/pl/plio.  Output: BUILD/NAME/NAME.bit and the
# logs BUILD/NAME/yosys.log, nextpnr.log (utilisation and Fmax).  The chip
# database is made once into BUILD/chipdb/xc7z010.bin (a few minutes).
# OPENXC7 is the installation (default $HOME/opt/openxc7, from the openXC7
# toolchain-installer's toolchain-sources-builder.sh; see ws286 p003).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
[ $# -ge 1 ] && [ -d "$1" ] || { echo "usage: pl-build.sh RTL_DIR [BUILD]" >&2; exit 2; }
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
rtl=$(cd "$1" && pwd)
name=$(basename "$rtl")
build=${2:-$root/build/zybo-pl}
openxc7=${OPENXC7:-$HOME/opt/openxc7}
part=xc7z010clg400-1
die=xc7z010
[ -f "$openxc7/export.sh" ] || { echo "pl-build: no openXC7 at $openxc7" >&2; exit 1; }
. "$openxc7/export.sh"
[ -f "$rtl/$name.v" ] && [ -f "$rtl/$name.xdc" ] || {
	echo "pl-build: $rtl needs $name.v and $name.xdc" >&2; exit 1; }
mkdir -p "$build/chipdb" "$build/$name"
out=$build/$name/$name
chipdb=$build/chipdb/$die.bin

if [ ! -f "$chipdb" ]; then
	echo "pl-build: making the chip database for $die"
	python3 "$openxc7/share/nextpnr/himbaechel/uarch/xilinx/gen/xilinx_gen.py" \
		--xray "$PRJXRAY_DB_DIR/zynq7" --device "$die" --bba "$build/chipdb/$die.bba"
	bbasm -l "$build/chipdb/$die.bba" "$chipdb.tmp"
	mv "$chipdb.tmp" "$chipdb"
fi

yosys -q -l "$build/$name/yosys.log" \
	-p "synth_xilinx -flatten -abc9 -arch xc7 -top $name; write_json $out.json" \
	"$rtl"/*.v
nextpnr-himbaechel --chipdb "$chipdb" --device "$part" --json "$out.json" \
	-o xdc="$rtl/$name.xdc" -o fasm="$out.fasm" \
	--log "$build/$name/nextpnr.log" ${PNR_ARGS:-} >/dev/null 2>"$build/$name/nextpnr.err" || {
	tail -20 "$build/$name/nextpnr.log" "$build/$name/nextpnr.err" >&2; exit 1; }
# fpga-as writes to its standard output: keep a failed run's partial file
# away from the name openFPGALoader is given.
fpga-as --prjxray_db_path="$PRJXRAY_DB_DIR/zynq7" --part "$part" "$out.fasm" > "$out.bit.tmp"
mv "$out.bit.tmp" "$out.bit"

# The utilisation (non-zero rows) and the post-route Fmax (the last report).
awk '/Device utilisation/ { u = 1; next }
	u && !/^Info:[ \t]+[A-Za-z0-9_]+: +[0-9]+\/ *[0-9]+/ { u = 0 }
	u && !/ 0\// { print }' "$build/$name/nextpnr.log"
grep 'Max frequency for clock' "$build/$name/nextpnr.log" | tail -1 || true
echo "pl-build: $out.bit $(wc -c < "$out.bit") bytes, sha256 $(sha256sum "$out.bit" | cut -c1-16)"
