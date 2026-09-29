#!/bin/sh
# ws035-p129: the Kei mark's layers (userland/desktop/artwork/mark.c) against a reference mark.c, on the host:
# every pixel of every layer at the sizes the programs use must be the same.  The reference is the mark.c of a git
# revision (default HEAD), or a file given.
#
#   plan/ws035/tests/p129/run-mark-compare.sh [REVISION | FILE]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=build/ws035-p129-host
mkdir -p "$out"
reference=${1:-HEAD}
if [ -f "$reference" ]; then
	cp "$reference" "$out/reference-mark.c"
else
	git show "$reference:userland/desktop/artwork/mark.c" > "$out/reference-mark.c"
fi
# The host's libm square root is fast; the guest's (src/libc/math/sqrt.c) is what makes the count of calls matter.
cc -O2 -Wall -Wextra -Werror -Iuserland/desktop/artwork -Dkeiland_mark_raster=reference_mark_raster \
    -c "$out/reference-mark.c" -o "$out/reference-mark.o"
cc -O2 -Wall -Wextra -Werror -Iuserland/desktop/artwork -c userland/desktop/artwork/mark.c -o "$out/mark.o"
cc -O2 -Wall -Wextra -Werror -Iuserland/desktop/artwork -c plan/ws035/tests/p129/mark-compare.c -o "$out/mark-compare.o"
cc "$out/mark-compare.o" "$out/mark.o" "$out/reference-mark.o" -lm -o "$out/mark-compare"
"$out/mark-compare"
