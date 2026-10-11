#!/bin/sh
# ws197-p007a1: the xHCI isochronous bookkeeping (src/drivers/pci/pci-xhci-isoch.c) on the host, normally and with
# ASan/UBSan, in a new folder of the build (old folders are Q1's to remove).  The last line is "xhci-isoch: all PASS".
# usage: sh plan/ws197/tests/run-xhci-isoch-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws197-isoch.XXXXXX")
flags="-std=c99 -Wall -Wextra -Werror -I$repo/include"
sources="$repo/plan/ws197/tests/xhci-isoch-test.c $repo/src/drivers/pci/pci-xhci-isoch.c"
# shellcheck disable=SC2086
cc $flags -O2 $sources -o "$work/isoch-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -o "$work/isoch-sanitized"
UBSAN_OPTIONS=halt_on_error=1 "$work/isoch-sanitized"
"$work/isoch-test"
