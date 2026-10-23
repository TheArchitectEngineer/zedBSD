#!/bin/sh
# Builds U-Boot SPL for the original ZYBO from the U-Boot v2025.07 release, for
# the JTAG boot (design §4): the SPL that jtag-boot.sh runs from OCM to set up
# the clocks, MIO, DDR and UART1, and then stops in board_boot_order.
#   plan/tools/zybo/spl-build.sh [BUILD]        (default build/zybo-spl)
# Output: BUILD/out/spl/u-boot-spl.bin (loaded) and BUILD/out/spl/u-boot-spl
# (the ELF, for the breakpoint's address).  The tarball is checked against the
# SHA-256 below, which p002 took from a download whose detached signature
# verified with Tom Rini's key 1A3C7F70E08FAB1707809BBF147C39FF9634B72C.
# U-Boot is GPL-2.0+: it is built here, outside the zedBSD tree and its make,
# and the binary is not committed or distributed.
# Needs the host's arm-linux-gnueabihf-gcc (Debian gcc-arm-linux-gnueabihf),
# bison, flex, bc, libssl-dev and python3-dev; not the zedBSD LLVM.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
build=${1:-$root/build/zybo-spl}
version=2025.07
tarball=u-boot-$version.tar.bz2
url=https://ftp.denx.de/pub/u-boot/$tarball
sha256=0f933f6c5a426895bf306e93e6ac53c60870e4b54cda56d95211bec99e63bec7
cross=${CROSS_COMPILE:-arm-linux-gnueabihf-}

mkdir -p "$build/dl"
if [ ! -f "$build/dl/$tarball" ]; then
	curl -sSfL -o "$build/dl/$tarball.part" "$url"
	mv -- "$build/dl/$tarball.part" "$build/dl/$tarball"
fi
echo "$sha256  $build/dl/$tarball" | sha256sum -c --quiet -
src=$build/u-boot-$version
if [ ! -f "$src/Makefile" ]; then
	tar -C "$build" -xjf "$build/dl/$tarball"
fi

# The changes to xilinx_zynq_virt_defconfig, applied as a fragment:
# - the ZYBO's device tree, which also picks board/xilinx/zynq/zynq-zybo/
#   ps7_init_gpl.c;
# - no RAM boot device: under the JTAG boot mode SPL would otherwise take
#   whatever DDR holds for an image and jump into it.  Without one it prints
#   "failed to boot from all boot devices" and hangs, which is safe even when
#   the breakpoint in board_boot_order is missed;
# - no mkeficapsule, a host tool SPL does not need (it wants gnutls).
cat > "$build/zybo-jtag.config" <<'EOF'
CONFIG_DEFAULT_DEVICE_TREE="zynq-zybo"
# CONFIG_SPL_RAM_DEVICE is not set
# CONFIG_TOOLS_MKEFICAPSULE is not set
EOF
out=$build/out
# The banner's date is the release's, so that the same source gives the same
# binary.
SOURCE_DATE_EPOCH=1751918313
export SOURCE_DATE_EPOCH
make -s -C "$src" O="$out" CROSS_COMPILE="$cross" xilinx_zynq_virt_defconfig
(cd "$src" && scripts/kconfig/merge_config.sh -m -O "$out" "$out/.config" \
	"$build/zybo-jtag.config" >/dev/null)
make -s -C "$src" O="$out" CROSS_COMPILE="$cross" olddefconfig
for option in 'CONFIG_DEFAULT_DEVICE_TREE="zynq-zybo"' \
	'# CONFIG_SPL_RAM_DEVICE is not set'; do
	grep -qx "$option" "$out/.config" || { echo "spl-build: $option did not take" >&2; exit 1; }
done
make -C "$src" O="$out" CROSS_COMPILE="$cross" DEVICE_TREE=zynq-zybo \
	-j"$(nproc)" spl/u-boot-spl.bin > "$build/make.out" 2>&1 ||
	{ tail -20 "$build/make.out" >&2; exit 1; }
[ -f "$out/spl/board/xilinx/zynq/zynq-zybo/ps7_init_gpl.o" ] ||
	{ echo "spl-build: the ZYBO's ps7_init_gpl.c was not built" >&2; exit 1; }
bp=$("${cross}nm" "$out/spl/u-boot-spl" | awk '$3 == "board_boot_order" { print $1 }')
[ -n "$bp" ] || { echo "spl-build: no board_boot_order in the ELF" >&2; exit 1; }
sha256sum "$out/spl/u-boot-spl.bin"
echo "spl-build: $(wc -c < "$out/spl/u-boot-spl.bin") bytes, board_boot_order 0x$bp"
