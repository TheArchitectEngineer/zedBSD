#!/bin/sh
# Checks the V3D 7.1 encoder (src/drivers/gpu/bcm2711/qpu71.c) with the unmodified fixed MIT Mesa packer and
# decoder in the ignored WS141 temp tree (ws279-p004).  Builds the Mesa oracle objects that the 7.1 lane model
# and the 7.1 shader test reuse.
# Usage: sh plan/ws279/tests/qpu71-format-host-test.sh [output-directory]
set -eu
out=${1:-build/ws279-qpu71-host}
mesa=plan/ws141/temp/mesa/src
mkdir -p "$out/mesa-host/util"
# Minimal standard helper declarations replace build-system-only Mesa util dependencies (the same shims as
# plan/ws141/tests/qpu-format-host-test.sh).  The encoder, decoder, enums and device description stay unmodified.
cat > "$out/mesa-host/util/macros.h" <<'HEADER'
#ifndef WS279_MESA_HOST_MACROS_H
#define WS279_MESA_HOST_MACROS_H
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define STATIC_ASSERT(e) _Static_assert(e, #e)
#define BITFIELD64_BIT(n) (UINT64_C(1) << (n))
#define BITFIELD64_RANGE(b,n) (((n) == 64 ? UINT64_MAX : ((UINT64_C(1) << (n)) - 1)) << (b))
#define UNREACHABLE(reason) abort()
#define ATTRIBUTE_CONST __attribute__((const))
#define ATTRIBUTE_PURE __attribute__((pure))
#endif
HEADER
cat > "$out/mesa-host/util/bitscan.h" <<'HEADER'
#ifndef WS279_MESA_HOST_BITSCAN_H
#define WS279_MESA_HOST_BITSCAN_H
#include <strings.h>
#endif
HEADER
sha256sum -c <<HASHES
abf436006dd3bed52cf1e77245d7267105a9100f02d8b3d643ddc6641fd6d551  $mesa/broadcom/qpu/qpu_pack.c
4b1b90e3bb8ea614dea36484e24d83049392c55cf19b01125627fa5ff4c9d07f  $mesa/broadcom/qpu/qpu_instr.h
f61745f35e5b34c74aad56b01afb9e068860ecb067361b8f9610dc1b673c8d4e  $mesa/broadcom/qpu/qpu_instr.c
HASHES
cc -std=gnu11 -Wall -Werror -I"$out/mesa-host" -I"$mesa" \
    -c "$mesa/broadcom/qpu/qpu_pack.c" -o "$out/mesa-pack.o"
cc -std=gnu11 -Wall -Werror -I"$out/mesa-host" -I"$mesa" \
    -c "$mesa/broadcom/qpu/qpu_instr.c" -o "$out/mesa-instr.o"
cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I"$out/mesa-host" -I"$mesa" \
    plan/ws279/tests/qpu71-format-host-test.c src/drivers/gpu/bcm2711/qpu71.c \
    "$out/mesa-pack.o" "$out/mesa-instr.o" -o "$out/qpu71-format-host-test"
"$out/qpu71-format-host-test"
