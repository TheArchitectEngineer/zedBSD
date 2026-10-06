#!/bin/sh
# ws169-p005: builds and runs the host test of the browser's sign-in codes of mail (host-browser-mail.c with
# userland/desktop/browser/shell/mail.c and fakes of libkeiland's system and the view's keys) under ASan and UBSan.
#   sh plan/ws169/tests/run-host-browser-mail.sh [OUTPUT]   (default build/ws169/host-browser-mail)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws169/host-browser-mail}
mkdir -p "$(dirname -- "$out")/include/keiland" "$(dirname -- "$out")/include/browser"
cp userland/desktop/include/keiland/keiland.h "$(dirname -- "$out")/include/keiland/keiland.h"
cp userland/desktop/include/browser/browser.h "$(dirname -- "$out")/include/browser/browser.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I. -Iuserland/desktop/browser -I"$(dirname -- "$out")/include" \
	plan/ws169/tests/host-browser-mail.c userland/desktop/browser/shell/mail.c -o "$out"
timeout 60 "$out"
