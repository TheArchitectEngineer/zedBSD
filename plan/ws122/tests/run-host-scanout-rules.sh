#!/bin/sh
# ws122-p005b: builds and runs the host test of the game mode's rules (userland/desktop/wayland/scanout-rules.c) under
# ASan and UBSan.
#   sh plan/ws122/tests/run-host-scanout-rules.sh [OUTPUT]   (default build/ws122-host/host-scanout-rules)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws122-host/host-scanout-rules}
mkdir -p "$(dirname "$out")"
${CC:-cc} -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I. \
	plan/ws122/tests/host-scanout-rules.c userland/desktop/wayland/scanout-rules.c -o "$out"
"$out"
