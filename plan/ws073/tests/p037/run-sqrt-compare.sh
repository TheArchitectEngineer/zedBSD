#!/bin/sh
# ws073-p037 (BUG-109): libm's sqrt and sqrtf (src/libc/math/sqrt.c, the processor's instruction on amd64) against the
# integer square root of a reference sqrt.c (default: the one of git revision REVISION, 5e4476b9 = before BUG-109), on
# the host: the same bits, errno, FE_INEXACT and FE_INVALID (libc's software exceptions, src/libc/fenv.c), and the
# time of each.  The library is compiled against zedBSD's headers like plan/tools/libm/host-test.sh.
#
#   plan/ws073/tests/p037/run-sqrt-compare.sh [REVISION | FILE]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=build/ws073-p037-host
mkdir -p "$out/obj"
cc=${HOSTCC:-clang}
reference=${1:-5e4476b9}
if [ -f "$reference" ]; then
	cp "$reference" "$out/reference-sqrt.c"
else
	git show "$reference:src/libc/math/sqrt.c" > "$out/reference-sqrt.c"
fi
flags="-std=c11 -O2 -fno-builtin -Wall -Wextra -Werror -Iinclude/libc -DKERN_UAPI_NATIVE -Iinclude -I."
# The library under test and libc's exceptions, the two renamed so the runner reaches them past the host's names.
objects=""
for source in src/libc/math/*.c; do
	name=$(echo "$source" | tr '/' '_')
	$cc $flags -c "$source" -o "$out/obj/$name.o"
	objects="$objects $out/obj/$name.o"
done
$cc $flags -Dfeclearexcept=zedbsd_feclearexcept -Dfetestexcept=zedbsd_fetestexcept \
    -Dferaiseexcept=zedbsd_feraiseexcept -c src/libc/fenv.c -o "$out/obj/fenv.o"
# The library's own calls go to the renamed exceptions too.
for object in $objects; do
	objcopy --redefine-sym feraiseexcept=zedbsd_feraiseexcept --redefine-sym feclearexcept=zedbsd_feclearexcept \
	    --redefine-sym fetestexcept=zedbsd_fetestexcept "$object"
done
# The reference root, renamed.
$cc $flags -Dsqrt=reference_sqrt -Dsqrtf=reference_sqrtf -c "$out/reference-sqrt.c" -o "$out/obj/reference-sqrt.o"
objcopy --redefine-sym feraiseexcept=zedbsd_feraiseexcept "$out/obj/reference-sqrt.o"
$cc -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror -c plan/ws073/tests/p037/sqrt-compare.c -o "$out/obj/sqrt-compare.o"
$cc -std=c11 -O2 -Wall -Wextra -Werror -c plan/tools/libm/errno-shim.c -o "$out/obj/errno-shim.o"
# shellcheck disable=SC2086
$cc -o "$out/sqrt-compare" "$out/obj/sqrt-compare.o" "$out/obj/errno-shim.o" "$out/obj/fenv.o" \
    "$out/obj/reference-sqrt.o" $objects
"$out/sqrt-compare"
