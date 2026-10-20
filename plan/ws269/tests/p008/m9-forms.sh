#!/bin/sh
# M9 of the USC operand forms: usc-encode.c's lower and upper source forms
# and destination forms against the encoders Mesa 26.2.5 generates in
# pco_isa.h, for random registers within each form's limits.
#   sh plan/ws269/tests/p008/m9-forms.sh MESA_SRC OUTDIR
# MESA_SRC is an extracted mesa-26.2.5 tree (src/imagination/pco is used).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
mesa=$1
out=$2
mkdir -p "$out/gen/util"
python3 "$mesa/src/imagination/pco/pco_isa.h.py" > "$out/gen/pco_isa.h"
python3 "$mesa/src/imagination/pco/pco_common.h.py" > "$out/gen/pco_common.h"
cat > "$out/gen/util/macros.h" <<'H'
#ifndef M9_UTIL_MACROS_H
#define M9_UTIL_MACROS_H
#include <assert.h>
#include <stdlib.h>
#define UNREACHABLE(s) abort()
#define unreachable(s) abort()
#define __constant
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BITFIELD_BIT(b) (1u << (b))
#define BITFIELD_MASK(b) ((b) == 32 ? 0xffffffffu : ((1u << (b)) - 1))
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#endif
H
cc -std=gnu11 -O1 -w -I"$root" -I"$root/include" -I"$root/src" -I"$out/gen" "$root/plan/ws269/tests/p008/m9-forms.c" -o "$out/m9-forms"
"$out/m9-forms"
