#!/bin/sh
# ws073-p038 (BUG-110): builds tls-check (dynamic, against BUILD's libc.so and ld.so; and static, against the sysroot's
# libc.a) and the loader's own TLS test (dyntest with tlstest.so, TLSDESC), copies them into the running guest over SSH
# (plan/tools/guest/guest.py; GUEST_RUNTIME names the guest) and runs them.  Passes when tls-check prints TLS-CHECK:PASS
# in both forms and dyntest runs to its end.
#
#   GUEST_RUNTIME=build/ws035-run plan/ws073/tests/p038/run-tls-check.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
build=${1:-build/amd64}
out=build/ws073-p038
mkdir -p "$out"
sysroot=$PWD/$build/sysroot
cc="$PWD/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"
guest() { timeout 300 python3 plan/tools/guest/guest.py "$@" 2>&1 </dev/null; }
status=0

# The dynamic program, compiled like a base program and linked with libc.so.
$cc -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC \
    -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
    -c plan/ws073/tests/p038/tls-check.c -o "$out/tls-check.o" || exit 1
$cc -m64 -nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
    -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$out/tls-check.o" -L"$build/dynamic" \
    -l:libc.so -o "$out/tls-check" || exit 1
# The static program, with the sysroot's C library.
# (the link of the sysroot's own smoke test, toolchain/llvm/sysroot.mk).
$cc -m64 -march=x86-64 -mno-red-zone -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -nostdinc -isystem "$sysroot/usr/include" \
    -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -Werror \
    -c plan/ws073/tests/p038/tls-check.c -o "$out/tls-check-static.o" || exit 1
$cc -m64 -nostdlib -static -Wl,--build-id=none -Wl,-T,"$sysroot/usr/lib/zedbsd/amd64/user.ld" \
    "$sysroot/usr/lib/crt0.o" "$out/tls-check-static.o" -Wl,--start-group "$sysroot/usr/lib/libc.a" \
    "$sysroot/usr/lib/libzedbsd-compiler-rt.a" "$sysroot/usr/lib/libclang_rt.builtins.a" -Wl,--end-group \
    -o "$out/tls-check-static" || exit 1

# Into the guest: the programs, and the image's loader test (dyntest and its modules in /lib).
guest put "$out/tls-check" /root/tls-check >/dev/null
guest put "$out/tls-check-static" /root/tls-check-static >/dev/null
guest put "$build/dynamic/dyntest" /root/dyntest >/dev/null
for module in tlstest.so rpathtest.so verstest.so versuse.so; do
	guest put "$build/dynamic/$module" "/lib/$module" >/dev/null
done
guest put "$build/dynamic/alt/rpathdep.so" /root/rpathdep.so >/dev/null

# The runs.
for program in tls-check tls-check-static; do
	guest run "chmod +x /root/$program; /root/$program" > "$out/$program.txt"
	tail -2 "$out/$program.txt" | sed "s/^/$program: /"
	grep -q '^TLS-CHECK:PASS' "$out/$program.txt" || status=1
done
guest run "chmod +x /root/dyntest; cd /root && ./dyntest" > "$out/dyntest.txt"
grep -E '^DL:' "$out/dyntest.txt" | tr '\n' ' ' | sed 's/^/dyntest: /'
echo
tail -3 "$out/dyntest.txt" | sed 's/^/dyntest: /'
echo "run-tls-check: status=$status"
exit $status
