#!/bin/sh
# ws113-p011: builds and runs the host test of the second output's claim rules (host-head-rules.c with
# src/drivers/gpu/i915/display/head-rules.c compiled unchanged).
#   sh plan/ws113/tests/host-head-rules.sh [OUTPUT]   (default build/ws113-host/host-head-rules)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws113-host/host-head-rules}
mkdir -p "$(dirname "$out")"
cc -std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -Isrc/drivers/gpu/i915/display \
	plan/ws113/tests/host-head-rules.c src/drivers/gpu/i915/display/head-rules.c -o "$out"
"$out"
