#!/bin/sh
# Builds plan/ws221/tests/ncm-ntb-frames-test.c with src/drivers/usb/usb-cdc-ncm.c on the host (HOSTCC, default cc;
# again with ASan and UBSan when the compiler has them) and runs it.  Outputs go to OUT (default build/ws221-ncm-frames,
# a new directory each run through plan/tools/fresh-out.sh).  The last line is "ncm-ntb-frames: PASS" from each run.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/fresh-out.sh
fresh_out "${OUT:-build/ws221-ncm-frames}"
out=${OUT:-build/ws221-ncm-frames}
compiler=${HOSTCC:-cc}
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Iinclude src/drivers/usb/usb-cdc-ncm.c \
	plan/ws221/tests/ncm-ntb-frames-test.c -o "$out/ncm-ntb-frames-test"
"$out/ncm-ntb-frames-test"
if "$compiler" -std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -fsanitize=address,undefined \
	-Iinclude src/drivers/usb/usb-cdc-ncm.c plan/ws221/tests/ncm-ntb-frames-test.c \
	-o "$out/ncm-ntb-frames-test-sanitized" 2>/dev/null; then
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$out/ncm-ntb-frames-test-sanitized"
else
	echo "ncm-ntb-frames: sanitizer run skipped (compiler unavailable)"
fi
