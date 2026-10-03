#!/bin/sh
# BUG-027 (ws046-p015): the SSH guest image (plan/tools/guest/config-amd64-ssh.mk) with the file-fault measurement:
# /bin/ffault and /bin/kbench (plan/tools/kbench, built against BUILD's libc), and an 80 MiB file at
# /var/bug027/libLLVM.so.23.1 as plain data (the ticket's file was the zedBSD libLLVM; only its size matters to the
# faults).  ws136-p001: the data is written here (the same bytes every time), or taken from LLVM_LIBRARY when given;
# it no longer reads another checkout's build.
#
#   [LLVM_LIBRARY=FILE] plan/ws046/tests/bug027/build-bug027-image.sh BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
build=${1:?usage: build-bug027-image.sh BUILD}
# The image first (BUILD's libc is what the programs link against), then the programs and the image again with them.
plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-ssh.mk "$build"
mkdir -p "$build/bug027"
bash plan/tools/kbench/build.sh "$build" "$build/bug027/ffault" ffault
bash plan/tools/kbench/build.sh "$build" "$build/bug027/kbench" kbench
data=$build/bug027/libLLVM.so.23.1
if [ -n "${LLVM_LIBRARY:-}" ]; then
	cp "$LLVM_LIBRARY" "$data"
elif [ ! -f "$data" ]; then
	python3 -c 'import random, sys; r = random.Random(27); sys.stdout.buffer.write(r.randbytes(80 * 1024 * 1024))' > "$data"
fi
exec plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-ssh.mk "$build" \
	--file /bin/ffault=$build/bug027/ffault --file /bin/kbench=$build/bug027/kbench \
	--file /var/bug027/libLLVM.so.23.1=$data
