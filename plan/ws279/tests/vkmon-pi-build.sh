#!/bin/sh
# Builds plan/ws279/tests/vkmon-pi.c (the System Monitor's draw, ws279-p007) for arm64
# against a libvulkan.so built in the same BUILD directory, with the compiler and the link line the dynamic userland
# rules use for vkquad.  The program is copied to the board by hand; it is not part of an image.
# Usage: sh plan/ws279/tests/vkmon-pi-build.sh BUILD-directory
set -eu
build=${1:?usage: vkmon-pi-build.sh BUILD-directory}
cc=build/llvm/bin/clang
sysroot=$(pwd)/build/arm64/sysroot
test -f "$build/dynamic/libvulkan.so"
"$cc" --target=aarch64-unknown-zedbsd -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" \
	-DHAL_ARCH_ARM64 -DKERN_USER_ABI_AARCH64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -march=armv8-a \
	-mno-outline-atomics -Os -ffreestanding -fPIC -fno-builtin -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror \
	-c plan/ws279/tests/vkmon-pi.c -o "$build/vkmon-pi.o"
"$cc" --target=aarch64-unknown-zedbsd --sysroot="$sysroot" -nostdlib -pie -Wl,--no-relax -Wl,--gc-sections \
	-Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
	-Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$build/vkmon-pi.o" \
	-L"$build/dynamic" -Wl,-rpath-link,"$build/dynamic" -l:libvulkan.so -l:libc.so -o "$build/vkmon-pi"
echo "built $build/vkmon-pi"
