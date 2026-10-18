#!/bin/sh
#
# Compiles the X68000 stage 2 from a base commit and from the work tree and
# compares the two objects' code.
#
# usage: x68k-stage2-compare.sh BASE_COMMIT OUT_DIR
#
# The tree has no definition of struct x68k_boot_handoff, so neither version
# compiles as it is.  Both are compiled with the same stand-in definition,
# written into OUT_DIR, which is enough to compare their code.  OUT_DIR must be
# a new directory under the work tree's build/.

set -eu

base=$1
out=$2
top=$(cd "$(dirname "$0")/../../.." && pwd)
cc=m68k-linux-gnu-gcc

mkdir -p "$out/old"
git -C "$top" archive "$base" bootloader include | tar -x -C "$out/old"

cat > "$out/handoff-stand-in.h" <<'HEADER'
#include "kern/boot.h"
struct boot_memory_region32 {
	uint32_t base;
	uint32_t size;
	uint32_t type;
};
struct x68k_boot_handoff {
	struct kern_boot_handoff common;
	uint32_t extension_magic;
	uint16_t extension_version;
	uint16_t extension_size;
	uint32_t ram_bytes;
	uint32_t kernel_phys_start;
	uint32_t kernel_phys_end;
	uint32_t loader_phys_start;
	uint32_t loader_phys_end;
	uint32_t memory_region_count;
	struct boot_memory_region32 memory_regions[4];
};
HEADER

flags="-nostdinc -Iinclude -Isrc -I. -Iinclude/libc -Isrc/hal/m68k -DHAL_ARCH_M68K -DHAL_BOARD_X68K -m68030 -msoft-float -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror -fno-builtin -fno-strict-aliasing"

cd "$top"
$cc -I"$out/old" -I"$out/old/include" $flags -include "$out/handoff-stand-in.h" -c "$out/old/bootloader/x68k/stage2.c" -o "$out/stage2-old.o"
$cc $flags -include "$out/handoff-stand-in.h" -c bootloader/x68k/stage2.c -o "$out/stage2-new.o"

m68k-linux-gnu-objdump -d -s "$out/stage2-old.o" | sed 1,3d > "$out/old.txt"
m68k-linux-gnu-objdump -d -s "$out/stage2-new.o" | sed 1,3d > "$out/new.txt"
if cmp -s "$out/old.txt" "$out/new.txt"; then
	echo "SAME: stage2 code is identical"
else
	echo "DIFF: stage2 code differs"
	diff "$out/old.txt" "$out/new.txt" | wc -l
fi
