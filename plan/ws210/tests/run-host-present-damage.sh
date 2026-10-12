#!/bin/sh
# ws210-p013 E6: builds and runs the host test of the swapchain images' stale parts (host-present-damage.c over
# userland/desktop/libkeiland/ui/present-damage.c) and of the copy with red and blue swapped (present-copy.c), with
# ASan and UBSan.
#   sh plan/ws210/tests/run-host-present-damage.sh [OUT]   (default build/ws210/host-present-damage)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws210/host-present-damage}
mkdir -p "$(dirname "$out")"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I. -Iuserland/desktop/include \
	userland/desktop/libkeiland/ui/present-damage.c userland/desktop/libkeiland/ui/present-copy.c plan/ws210/tests/host-present-damage.c -o "$out"
"$out"
