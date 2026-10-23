#!/bin/sh
# Builds U-Boot SPL for the original ZYBO from the U-Boot v2025.07 release.
# One SPL serves both boots.  The JTAG boot (WS286 design §4): jtag-boot.sh
# runs it from OCM to set up the clocks, MIO, DDR and UART1, and stops it in
# board_boot_order.  The SD boot (WS289 design §5): the boot ROM runs it from
# the card's BOOT.BIN, and it boots the kernel in Falcon mode, reading
# system.dtb and the FIT vmunix.itb from the card's first partition (FAT)
# and entering the kernel with r2 = the DTB, as the JTAG boot does.
#   plan/tools/zybo/spl-build.sh [BUILD]        (default build/zybo-spl)
# Output: BUILD/out/spl/u-boot-spl.bin (loaded over JTAG), BUILD/out/spl/
# u-boot-spl (the ELF, for the breakpoint's address) and BUILD/out/spl/
# boot.bin (the boot ROM's image of it: the card's BOOT.BIN, for
# make ZYBO_BOOT_BIN=... sd-image).  The tarball is checked against the
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
# merge_config.sh runs in the source tree, so the paths are made absolute.
mkdir -p "$build"
build=$(cd "$build" && pwd)
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
# - no mkeficapsule, a host tool SPL does not need (it wants gnutls);
# - Falcon mode's files (the defconfig already has SPL_OS_BOOT, and the
#   Zynq's spl_start_uboot always boots the OS): the kernel is the FIT
#   vmunix.itb, whose image SPL copies to its load address and nothing
#   below it (a legacy uImage would put its 64-byte header at 0x000FFFC0,
#   the alias of the high OCM where CPU1 waits), and the DTB system.dtb goes
#   where the JTAG boot puts it, 0x01F00000;
# - SPL's own DDR (BSS, stack, malloc pool) moved to the top 16 MiB of the
#   512 MiB: the defconfig puts it at 0x00100000-0x02200000, over the kernel
#   and the DTB it loads.  The pool holds the whole FIT while SPL copies the
#   kernel out of it; 12 MiB, not the defconfig's 32, fits below the stack.
# Every line is checked after the merge, which drops an option whose
# dependencies are not met without a word.
cat > "$build/zybo.config" <<'EOF'
CONFIG_DEFAULT_DEVICE_TREE="zynq-zybo"
# CONFIG_SPL_RAM_DEVICE is not set
# CONFIG_TOOLS_MKEFICAPSULE is not set
CONFIG_SPL_FS_LOAD_KERNEL_NAME="vmunix.itb"
CONFIG_SPL_FS_LOAD_ARGS_NAME="system.dtb"
CONFIG_SPL_PAYLOAD_ARGS_ADDR=0x01f00000
CONFIG_SPL_BSS_START_ADDR=0x1f000000
CONFIG_SPL_STACK_R_ADDR=0x1ff00000
CONFIG_SPL_HAS_CUSTOM_MALLOC_START=y
CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR=0x1f100000
CONFIG_SPL_SYS_MALLOC_SIZE=0xc00000
EOF
out=$build/out
# The banner's date is the release's, so that the same source gives the same
# binary.
SOURCE_DATE_EPOCH=1751918313
export SOURCE_DATE_EPOCH
make -s -C "$src" O="$out" CROSS_COMPILE="$cross" xilinx_zynq_virt_defconfig
(cd "$src" && scripts/kconfig/merge_config.sh -m -O "$out" "$out/.config" \
	"$build/zybo.config" >/dev/null)
make -s -C "$src" O="$out" CROSS_COMPILE="$cross" olddefconfig
while IFS= read -r option; do
	grep -qx "$option" "$out/.config" || { echo "spl-build: $option did not take" >&2; exit 1; }
done < "$build/zybo.config"
grep -qx 'CONFIG_SPL_OS_BOOT=y' "$out/.config" ||
	{ echo "spl-build: the defconfig no longer has SPL_OS_BOOT" >&2; exit 1; }
make -C "$src" O="$out" CROSS_COMPILE="$cross" DEVICE_TREE=zynq-zybo \
	-j"$(nproc)" spl/u-boot-spl.bin spl/boot.bin > "$build/make.out" 2>&1 ||
	{ tail -20 "$build/make.out" >&2; exit 1; }
[ -f "$out/spl/board/xilinx/zynq/zynq-zybo/ps7_init_gpl.o" ] ||
	{ echo "spl-build: the ZYBO's ps7_init_gpl.c was not built" >&2; exit 1; }
bp=$("${cross}nm" "$out/spl/u-boot-spl" | awk '$3 == "board_boot_order" { print $1 }')
[ -n "$bp" ] || { echo "spl-build: no board_boot_order in the ELF" >&2; exit 1; }
# SPL runs from the OCM's 192 KiB, and its DDR is where the fragment put it.
size=$(wc -c < "$out/spl/u-boot-spl.bin")
[ "$size" -le $((0x30000)) ] ||
	{ echo "spl-build: SPL is $size bytes, past the OCM's 192 KiB" >&2; exit 1; }
bss=$("${cross}nm" "$out/spl/u-boot-spl" | awk '$3 == "__bss_start" { print $1 }')
[ "$bss" = 1f000000 ] ||
	{ echo "spl-build: SPL's BSS starts at 0x$bss, not 0x1f000000" >&2; exit 1; }
sha256sum "$out/spl/u-boot-spl.bin" "$out/spl/boot.bin"
echo "spl-build: $size bytes, board_boot_order 0x$bp, BSS 0x$bss"
