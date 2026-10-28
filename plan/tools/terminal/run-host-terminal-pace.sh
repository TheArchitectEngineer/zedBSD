#!/bin/sh
# The host test of the terminal's frame pacing (host-terminal-pace.c over userland/desktop/terminal/pace.h): motions
# between two answers of the compositor draw one frame, a key draws at once with one frame ahead at most, the watchdog
# and a frame not shown.  With ASan and UBSan.
#   sh plan/tools/terminal/run-host-terminal-pace.sh [OUT]   (default build/ws210/host-terminal-pace)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws210/host-terminal-pace}
mkdir -p "$(dirname "$out")"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I. -Iuserland/desktop/include \
	plan/tools/terminal/host-terminal-pace.c -o "$out"
"$out"
