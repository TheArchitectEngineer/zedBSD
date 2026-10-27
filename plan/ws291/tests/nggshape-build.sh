#!/bin/sh
# Builds plan/ws291/tests/nggshape/ into BUILD/bin/nggshape with the image's compile and link lines (as
# plan/ws039/tests/vkhang-build.sh): glslc makes the two programs' SPIR-V into a header first.
#   usage: plan/ws291/tests/nggshape-build.sh BUILD   (for example build/ws291/usb; the image is built first)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
BUILD=${1:?usage: nggshape-build.sh BUILD}
TOP=$(pwd)
SYSROOT=$TOP/build/amd64/sysroot
CC=$TOP/build/llvm/bin/clang
OBJ=$BUILD/dynamic/obj/plan/ws291/tests/nggshape
mkdir -p "$OBJ" "$BUILD/bin"
{
	echo "/* Made by plan/ws291/tests/nggshape-build.sh from nggshape/shape.vert, heavy.vert, shape.frag, mview-like.frag and mview's mview.vert and mview.frag; do not edit. */"
	for stage in vert frag; do
		glslc -mfmt=c "plan/ws291/tests/nggshape/shape.$stage" -o "$OBJ/shape-$stage.inc"
		echo "static const uint32_t nggshape_shape_${stage}[] ="
		cat "$OBJ/shape-$stage.inc"
		echo ";"
	done
	glslc -mfmt=c "plan/ws291/tests/nggshape/heavy.vert" -o "$OBJ/heavy-vert.inc"
	echo "static const uint32_t nggshape_heavy_vert[] ="
	cat "$OBJ/heavy-vert.inc"
	echo ";"
	glslc -mfmt=c "userland/tests/mview/shaders/mview.vert" -o "$OBJ/mview-vert.inc"
	echo "static const uint32_t nggshape_mview_vert[] ="
	cat "$OBJ/mview-vert.inc"
	echo ";"
	glslc -mfmt=c "userland/tests/mview/shaders/mview.frag" -o "$OBJ/mview-frag.inc"
	echo "static const uint32_t nggshape_mview_real_frag[] ="
	cat "$OBJ/mview-frag.inc"
	echo ";"
	glslc -mfmt=c "plan/ws291/tests/nggshape/mview-like.frag" -o "$OBJ/mview-like-frag.inc"
	echo "static const uint32_t nggshape_mview_frag[] ="
	cat "$OBJ/mview-like-frag.inc"
	echo ";"
} > "$OBJ/nggshape-programs.h"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -nostdinc -I. -Iinclude -I"$OBJ" -isystem "$SYSROOT/usr/include" \
	-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC \
	-fno-builtin -fno-stack-protector -fasynchronous-unwind-tables -ftls-model=global-dynamic -Wall -Wextra -Werror \
	-c plan/ws291/tests/nggshape/main.c -o "$OBJ/main.o"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -m64 -nostdlib -pie -Wl,--no-relax \
	-Wl,--hash-style=gnu,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
	-Wl,--dynamic-linker=/lib/ld.so "$SYSROOT/usr/lib/crt1.o" "$OBJ/main.o" \
	-L"$BUILD/dynamic" -Wl,-rpath-link,"$BUILD/dynamic" -l:libvulkan.so -l:libc.so -o "$BUILD/bin/nggshape"
python3 tools/build/check-dynamic-elf.py --machine amd64 --role application --needed libvulkan.so --needed libc.so "$BUILD/bin/nggshape"
