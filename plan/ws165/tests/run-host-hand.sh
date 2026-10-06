#!/bin/sh
# ws165-p002: the host test of the handwriting recognizer (host-hand.c): the templates of the package hand-hershey
# (fetched, checked and converted by its make rule into build/packages/hand-hershey/hershey.txt), then SAMPLES
# made-up samples of each character recognized; top-1, top-4 and the time printed.
#   sh plan/ws165/tests/run-host-hand.sh [SAMPLES [SEED]]   (from anywhere; default 20 samples)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws165-host
mkdir -p "$out"
make -s ZEDBSD_CONFIG=config/ci/config-amd64.mk hand-hershey >/dev/null
cc -std=c11 -D_DEFAULT_SOURCE -O2 -g -Wall -Wextra -Werror -I. -o "$out/host-hand" plan/ws165/tests/host-hand.c \
	userland/desktop/wayland/hand-cloud.c -lm
exec timeout 600 "$out/host-hand" build/packages/hand-hershey/hershey.txt "${1:-20}" ${2:-}
