#!/bin/sh
# ws154-p003: builds and runs the host test of the SKK engine (host-skk.c with userland/desktop/ime/skk-engine.c and the
# Japanese engine's romaji, kana, dictionary and user dictionary parts), under ASan and UBSan with automatic variables
# filled with a pattern (an unset one shows), with skk-test.dict and then the image's dictionaries (skk-dict/).
# Last line: host-skk: PASS.
#   sh plan/ws154/tests/run-host-skk.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=build/ws154-host
rm -rf "$out"
mkdir -p "$out/work"
D=userland/desktop/ime
${CC:-cc} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -ftrivial-auto-var-init=pattern -I$D \
    plan/ws154/tests/host-skk.c $D/output.c $D/ja-kana.c $D/ja-romaji.c $D/ja-dict.c $D/ja-user.c $D/skk-engine.c \
    -pthread -o "$out/host-skk" || { echo "host-skk: FAIL (build)"; exit 1; }
timeout 60 "$out/host-skk" plan/ws154/tests/skk-test.dict "$out/work" $D/skk-dict/SKK-JISYO.X $D/skk-dict/SKK-JISYO.remacs
