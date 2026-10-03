#!/bin/sh
# Builds this tree's /bin/sh for the amd64 guest as a dynamic program
# against the libc.so of an existing guest image's build, so that it can be
# copied into a running guest (a copy of that image) and tried there without
# building a new image.  From ws045's build-guest-utils.sh (ws065-p004).
#
#   sh plan/tools/sh/build-guest-sh.sh [IMAGE_BUILD] [OUTPUT]
#
# IMAGE_BUILD is the build directory of the image the guest runs (default
# /home/awe/zedBSD-rpi4/build/ws053-full-hal-guest, read only); its
# dynamic/libc.so is linked against.  The headers and crt1.o come from this
# tree's sysroot (build/amd64/sysroot, made by make sysroot-amd64).
# OUTPUT defaults to build/guest-sh/sh.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
image_build=${1:-/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest}
out=${2:-build/guest-sh/sh}
sysroot=build/amd64/sysroot
clang=build/llvm/bin/clang
objects=$(dirname "$out")/.obj
mkdir -p "$objects"

cflags="--target=x86_64-unknown-zedbsd --sysroot=$sysroot -m64 -march=x86-64 \
-mno-red-zone -Os -ffreestanding -fPIC -fno-builtin -fno-stack-protector \
-Wall -Wextra -nostdinc -I. -Iinclude -Iuserland/base/libedit \
-isystem $sysroot/usr/include -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64"
link="--target=x86_64-unknown-zedbsd --sysroot=$sysroot -m64 -nostdlib -pie \
-Wl,--no-relax -Wl,--gc-sections -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
-Wl,-z,stack-size=0x100000,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so \
$sysroot/usr/lib/crt1.o"
libs="-L$image_build/dynamic -Wl,-rpath-link,$image_build/dynamic -l:libc.so"

# The shell, its line editor and the command helpers.
programs=""
for source in userland/base/sh/*.c userland/base/libedit/readline.c \
    userland/base/common/command.c; do
	object=$objects/$(printf '%s' "$source" | tr '/.' '__').o
	# shellcheck disable=SC2086
	$clang $cflags -c "$source" -o "$object"
	programs="$programs $object"
done
# shellcheck disable=SC2086
$clang $link $programs $libs -o "$out"
echo "$out"
