#!/bin/sh
# ws166-p002: builds and runs the host test of the predictions (host-predict.c with userland/desktop/ime/ja-predict.c
# and the Japanese engine's dictionary and user dictionary parts), under ASan and UBSan, with predict-test.dict, then
# times them with the image's Japanese dictionary (userland/desktop/ime/dict/SKK-JISYO.ja).  Last line: host-predict: PASS.
#   sh plan/ws166/tests/run-host-predict.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=build/ws166-host
rm -rf "$out"
mkdir -p "$out/work"
D=userland/desktop/ime
${CC:-cc} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -ftrivial-auto-var-init=pattern -I$D \
    plan/ws166/tests/host-predict.c $D/ja-kana.c $D/ja-dict.c $D/ja-user.c $D/ja-predict.c \
    -pthread -o "$out/host-predict" || { echo "host-predict: FAIL (build)"; exit 1; }
timeout 60 "$out/host-predict" plan/ws166/tests/predict-test.dict "$out/work" $D/dict/SKK-JISYO.ja
