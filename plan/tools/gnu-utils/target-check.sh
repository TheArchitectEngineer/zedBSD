#!/bin/sh
# ws045: compiles C files of userland/base for the zedBSD targets with the
# flags the image build uses (-Wall -Wextra -Werror), against the sysroot
# headers of an earlier build, so that a warning is found without building
# an image.  Compiles only; nothing is linked or installed.
#
#   sh plan/tools/gnu-utils/target-check.sh [SYSROOT_BUILD] FILE.c...
#
# SYSROOT_BUILD is the build directory whose amd64/ and i386/ sysroots are
# used (default this tree's build, read only; ws136-p003: it was another
# tree's, now gone); arm64 compiles against include/libc, as its image build
# does.
set -eu
build=build
case ${1:-} in
*.c) ;;
*) build=$1; shift ;;
esac
clang=build/llvm/bin/clang
out=build/ws045/target-check
mkdir -p "$out"
status=0
for file in "$@"; do
	object=$out/$(printf '%s' "$file" | tr '/.' '__')
	if ! "$clang" --target=x86_64-unknown-zedbsd --sysroot="$build/amd64/sysroot" -m64 -march=x86-64 -mno-red-zone \
		-ffreestanding -fno-pic -fno-pie -fno-builtin -fno-common -Os \
		-Wall -Wextra -Werror -nostdinc \
		-isystem "$build/amd64/sysroot/usr/include" -Iinclude -Isrc -I. \
		-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -c "$file" -o "$object.amd64.o"; then
		status=1
	fi
	if ! "$clang" --target=i386-unknown-zedbsd --sysroot="$build/i386/sysroot" -m32 -march=i386 -ffreestanding \
		-fno-pic -fno-pie -fno-builtin -fno-common -Os \
		-Wall -Wextra -Werror -nostdinc \
		-isystem "$build/i386/sysroot/usr/include" -Iinclude -Isrc -I. -DHAL_ARCH_I386 \
		-c "$file" -o "$object.i386.o"; then
		status=1
	fi
	# arm64 builds its userland against include/libc directly.
	if ! "$clang" --target=aarch64-unknown-zedbsd -march=armv8-a \
		-mno-outline-atomics -ffreestanding -fno-pic -fno-pie \
		-fno-builtin -fno-common -Os -Wall -Wextra -Werror -nostdinc \
		-Iinclude -Isrc -I. -Iinclude/libc -DHAL_ARCH_ARM64 \
		-DKERN_USER_ABI_AARCH64 -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE \
		-c "$file" -o "$object.arm64.o"; then
		status=1
	fi
done
[ $status -eq 0 ] && echo "target-check: $# files, amd64, i386 and arm64, no warning"
exit $status
