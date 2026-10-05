#!/bin/sh
# ws066-p001: builds the programs startup-measure.sh runs in the guest, on the host with the target's clang, against
# BUILD's libc.so (BUILD is the build the guest's image came from):
#   startbench        times COUNT starts of a program (startbench.c)
#   true-static       true.c linked statically with the sysroot's libc.a
#   true-sysv         true.c linked dynamically as the base programs are (--hash-style=sysv, platform/amd64/vmunix.mk)
#   true-gnu          the same with --hash-style=gnu
#   bsymf/libc.so     BUILD's libc.so linked again from its objects with -Bsymbolic-functions (the candidate that binds
#                     the library's calls to its own functions at link time)
#   t.c               the file `cc t.c -o t` compiles
# and puts them in OUT/../measure.tar.
#
#   plan/ws066/tests/build-measure.sh BUILD OUT      (OUT is made afresh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
[ $# -eq 2 ] || { echo "usage: build-measure.sh BUILD OUT" >&2; exit 2; }
build=$1
out=$2
tests=plan/ws066/tests
config=ZEDBSD_CONFIG=config/ci/config-amd64.mk
sysroot=$PWD/build/amd64/sysroot
cc="$PWD/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"
lld=$PWD/build/llvm/bin/ld.lld
cflags="-nostdinc -I. -Iinclude -isystem $sysroot/usr/include -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC
 -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC -fno-builtin -fno-stack-protector -Wall -Wextra -Werror"
program="-m64 -nostdlib -pie -Wl,--no-relax -Wl,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000
 -Wl,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so $sysroot/usr/lib/crt1.o"
libs="-L$build/dynamic -Wl,-rpath-link,$build/dynamic -l:libc.so"
rm -rf "$out"
mkdir -p "$out/obj" "$out/bsymf"

# BUILD's libc.so and its objects (made if they are not there yet; the sysroot is not rebuilt: make -n checks it).
if [ "$(make -n $config BUILD="$build" "$build/dynamic/libc.so" 2>&1 | grep -c 'sysroot.mk\|zedbsd-sysroot-complete')" != 0 ]; then
	echo "build-measure: making $build/dynamic/libc.so would rebuild the sysroot; stopping" >&2
	exit 1
fi
make $config BUILD="$build" "$build/dynamic/libc.so" >/dev/null

# The programs.
$cc $cflags -c $tests/startbench.c -o "$out/obj/startbench.o"
$cc $program -Wl,--hash-style=sysv "$out/obj/startbench.o" $libs -o "$out/startbench"
$cc $cflags -c $tests/true.c -o "$out/obj/true.o"
$cc $program -Wl,--hash-style=sysv "$out/obj/true.o" $libs -o "$out/true-sysv"
$cc $program -Wl,--hash-style=gnu "$out/obj/true.o" $libs -o "$out/true-gnu"
$cc -m64 -march=x86-64 -mno-red-zone -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -nostdinc -isystem "$sysroot/usr/include" \
    -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror \
    -c $tests/true.c -o "$out/obj/true-static.o"
$cc -m64 -nostdlib -static -Wl,--build-id=none -Wl,-T,"$sysroot/usr/lib/zedbsd/amd64/user.ld" \
    "$sysroot/usr/lib/crt0.o" "$out/obj/true-static.o" -Wl,--start-group "$sysroot/usr/lib/libc.a" \
    "$sysroot/usr/lib/libzedbsd-compiler-rt.a" "$sysroot/usr/lib/libclang_rt.builtins.a" -Wl,--end-group \
    -o "$out/true-static"

# libc.so again with -Bsymbolic-functions: the objects and flags of its rule in platform/amd64/vmunix.mk.
objects=$(make -pn $config BUILD="$build" "$build/dynamic/libc.so" 2>/dev/null |
    sed -n 's/^DYNAMIC_LIBC_OBJS := //p' | head -1)
[ -n "$objects" ] || { echo "build-measure: no DYNAMIC_LIBC_OBJS" >&2; exit 1; }
$lld -m elf_x86_64 -shared -soname libc.so --hash-style=both -z now -z relro -z separate-code -z stack-size=0x100000 \
    -Bsymbolic-functions $objects -o "$out/bsymf/libc.so"
printf 'int main(void) { return 0; }\n' > "$out/t.c"

# The relocations each libc.so leaves to the loader.
for library in "$build/dynamic/libc.so" "$out/bsymf/libc.so"; do
	echo "build-measure: $library symbol relocations: $(build/llvm/bin/llvm-readelf -r "$library" |
	    grep -c 'R_X86_64_\(JUMP_SLOT\|GLOB_DAT\|64\) ' || true)"
done

# One archive for the guest.
(cd "$out" && tar --format=ustar -cf ../measure.tar startbench true-static true-sysv true-gnu bsymf t.c)
echo "build-measure: $(dirname -- "$out")/measure.tar"
