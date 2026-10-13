#!/bin/sh
# ws102-p026: builds and runs the host test of the on-screen keyboard's words from the Japanese engine (host-predict.c:
# the reading converted whole, okurigana and all, before the predictions) with the engine's sources and WS095's small
# dictionary, on Linux, under ASan and UBSan.
#   sh plan/ws102/tests/host-predict.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=build/ws102-host
mkdir -p "$out"
D=userland/desktop/ime
${CC:-clang} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I$D \
	plan/ws102/tests/host-predict.c $D/output.c $D/ja-kana.c $D/ja-romaji.c $D/ja-dict.c $D/ja-user.c \
	$D/ja-inflect.c $D/ja-segment.c $D/ja-engine.c $D/ja-keys.c $D/ja-predict.c -pthread -o "$out/host-predict"
"$out/host-predict" plan/ws095/tests/ja-test.dict
