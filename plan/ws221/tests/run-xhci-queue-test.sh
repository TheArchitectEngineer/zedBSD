#!/bin/sh
# Builds plan/ws221/tests/xhci-queue-test.c (the xHCI bulk endpoint queue decisions of
# include/drivers/pci/pci-xhci-queue.h) on the host (HOSTCC, default cc), once optimized and once with ASan and
# UBSan, and runs both.  Outputs go to OUT (default build/ws221-xhci-queue, a new directory each run through
# plan/tools/fresh-out.sh).  The last line of each run is "xhci-queue: PASS".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/fresh-out.sh
fresh_out "${OUT:-build/ws221-xhci-queue}"
out=${OUT:-build/ws221-xhci-queue}
compiler=${HOSTCC:-cc}
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Iinclude plan/ws221/tests/xhci-queue-test.c -o "$out/xhci-queue-test"
"$out/xhci-queue-test"
"$compiler" -std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -fsanitize=address,undefined \
	-fno-sanitize-recover=all -Iinclude plan/ws221/tests/xhci-queue-test.c -o "$out/xhci-queue-test-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$out/xhci-queue-test-sanitized"
