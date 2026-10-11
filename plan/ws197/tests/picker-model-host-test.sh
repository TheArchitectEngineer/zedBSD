#!/bin/sh
# The host test of libkeiland's media picker's model (ws197-p014, plan/ws197/phase014/phase.md): builds
# plan/ws197/tests/picker-model-host-test.c with userland/desktop/libkeiland/ui/picker-model.c and the Linux libkeiland
# (for kl_scroll and kl_image) under ASan and UBSan, and runs it in a new folder of the build.  The last line is
# "picker-model-host-test: PASS" or "... FAIL".
# usage: plan/ws197/tests/picker-model-host-test.sh   (from the repository's top, after
#   make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=$LINUX $LINUX/lib/libkeiland.so; LINUX= and OUT= to choose)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${OUT:-build/ws197-picker-host}
LINUX=${LINUX:-build/ws197-p014-linux}
mkdir -p "$OUT"
run=$(mktemp -d "$OUT/run.XXXXXX")
flags="-std=gnu17 -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. -Iuserland/desktop/include -I$LINUX/include"
cc $flags -o "$OUT/picker-model-host-test" plan/ws197/tests/picker-model-host-test.c userland/desktop/libkeiland/ui/picker-model.c \
	-L"$LINUX/lib" -Wl,-rpath-link,"$LINUX/lib" -lkeiland -lpthread
LD_LIBRARY_PATH="$LINUX/lib" timeout 60 "$OUT/picker-model-host-test" "$PWD/$run"
