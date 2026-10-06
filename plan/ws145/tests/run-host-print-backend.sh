#!/bin/sh
# ws145-p003: builds libkeiland-backend's printers (print.c) with host-print-backend.c and keiland-printd on the host
# (ASan and UBSan) and runs them against mock-printers.py, in a fresh folder each run (under OUTPUT's folder; Q1's cleaning
# removes the old ones).
#   sh plan/ws145/tests/run-host-print-backend.sh [OUTPUT]   (default build/ws145/host-print-backend)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws145/host-print-backend}
dir=$(dirname -- "$out")
mkdir -p "$dir"
flags="-std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
cc $flags -I. -Iuserland/desktop/libkeiland-backend plan/ws145/tests/host-print-backend.c \
	userland/desktop/libkeiland-backend/print/print.c -o "$out"
cc $flags -Iuserland/desktop/printd userland/desktop/printd/*.c -o "$dir/keiland-printd"
folder=$(mktemp -d "$dir/print-backend.XXXXXX")
mkdir -m 700 "$folder/runtime"
python3 -c 'import os, sys; open(sys.argv[1], "wb").write(b"%PDF-1.4\n" + os.urandom(100000) + b"\n%%EOF\n")' "$folder/doc.pdf"
python3 plan/ws145/tests/mock-printers.py "$folder/printers" > "$folder/ports" &
mock=$!
trap 'kill $mock 2>/dev/null' EXIT
tries=0
while ! grep -q PORTS "$folder/ports" 2>/dev/null; do
	tries=$((tries + 1))
	[ $tries -lt 50 ] || { echo "mock-printers did not start"; exit 1; }
	sleep 0.1
done
set -- $(cat "$folder/ports")
ASAN_OPTIONS=detect_leaks=0 timeout 120 "$out" "$(pwd)/$dir/keiland-printd" "$(pwd)/$folder" "$2" "$3" "$folder/doc.pdf"
cmp "$folder/doc.pdf" "$folder/printers/ipp-1.pdf" && echo "PASS ipp-document" || { echo "FAIL ipp-document"; exit 1; }
cmp "$folder/doc.pdf" "$folder/printers/lpd-1.data" && echo "PASS lpd-document" || { echo "FAIL lpd-document"; exit 1; }
