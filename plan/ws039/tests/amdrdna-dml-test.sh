#!/bin/sh
# Host test (m) of ws039-p015: the amdrdna display plan (src/drivers/gpu/amdrdna/mode-plan.c) against the reference
# display mode library (Linux v6.19 drivers/gpu/drm/amd/display/dc/dml: display_mode_vba.c, dcn20/display_mode_vba_20v2.c,
# dcn20/display_rq_dlg_calc_20v2.c, display_rq_dlg_helpers.c, calcs/dcn_calc_math.c, and the dcn2_0_ip and dcn2_0_soc
# tables cut out of dcn20/dcn20_fpu.c), built here from the temp tree with the shims of dml-oracle/ and never kept in
# the tree.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-dml-test.sh
#   The Linux tree is plan/ws039/temp/linux (AMDRDNA_LINUX overrides it).  Needs cc.  Everything goes in a new
#   directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
linux=${AMDRDNA_LINUX:-$repo/plan/ws039/temp/linux}
dc="$linux/drivers/gpu/drm/amd/display/dc"
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/amdrdna-dml.XXXXXX")
include="-I$tests/dml-oracle -I$dc/dml -I$dc/inc -I$dc"
# The reference's files, compiled where they are (their own includes are relative to them).
for file in dml/display_mode_vba.c dml/dcn20/display_mode_vba_20v2.c dml/dcn20/display_rq_dlg_calc_20v2.c \
	dml/display_rq_dlg_helpers.c dml/calcs/dcn_calc_math.c; do
	cc -std=gnu11 -O1 -g -c -w $include "$dc/$file" -o "$out/reference-$(basename "$file" .c).o"
done
# The bounding box tables, cut out of the reference driver's file.
{
	echo '#include "display_mode_lib.h"'
	awk '/^struct _vcs_dpi_ip_params_st dcn2_0_ip = \{/,/^\};/' "$dc/dml/dcn20/dcn20_fpu.c"
	awk '/^struct _vcs_dpi_soc_bounding_box_st dcn2_0_soc = \{/,/^\};/' "$dc/dml/dcn20/dcn20_fpu.c"
} > "$out/bounding-box.c"
cc -std=gnu11 -O1 -g -c -w $include "$out/bounding-box.c" -o "$out/bounding-box.o"
cc -std=gnu11 -O1 -g -c -Wall -Wextra -Werror $include "$tests/dml-oracle/harness.c" -o "$out/harness.o"
# The plan and the test.
cc -std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE -I"$repo/include" -I"$repo/src" -I"$repo" -I"$tests" \
	-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-include uapi/errno.h -o "$out/amdrdna-mode-plan-test" "$tests/amdrdna-mode-plan-test.c" \
	"$repo/src/drivers/gpu/amdrdna/mode-plan.c" "$out/harness.o" "$out/bounding-box.o" "$out"/reference-*.o -lm
if ASAN_OPTIONS=detect_leaks=0 "$out/amdrdna-mode-plan-test"; then
	echo "PASS amdrdna-dml-test ($out)"
else
	echo "FAIL amdrdna-dml-test ($out)"
	exit 1
fi
