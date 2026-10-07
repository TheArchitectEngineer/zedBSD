#!/bin/sh
# ws145-p002: builds keiland-printd on the host (ASan and UBSan) and drives it with printd-test.py against the mock IPP
# printer and LPD queue (mock-printers.py), in a fresh folder each run (under OUTPUT's folder; Q1's cleaning removes the
# old ones).
#   sh plan/ws145/tests/run-host-printd.sh [OUTPUT]   (default build/ws145/keiland-printd)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws145/keiland-printd}
dir=$(dirname -- "$out")
mkdir -p "$dir"
cc -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -Iuserland/desktop/printd userland/desktop/printd/*.c -o "$out"
folder=$(mktemp -d "$dir/printd.XXXXXX")
ASAN_OPTIONS=detect_leaks=0 timeout 120 python3 plan/ws145/tests/printd-test.py "$out" "$folder"
