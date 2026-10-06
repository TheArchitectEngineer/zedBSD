#!/bin/sh
# ws175-p006: builds libz-compat (inflate, deflate, checksums) and host-deflate with the host's C compiler under ASan and
# UBSan, runs it, and has the host's zlib (python3) read back every stream it compressed.
#   sh plan/ws175/tests/run-host-deflate.sh [OUTPUT]   (default build/ws175-deflate)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws175-deflate}
cc=${CC:-cc}
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
"$cc" -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I"$out/include" \
	userland/base/libz-compat/inflate.c userland/base/libz-compat/deflate.c userland/base/libz-compat/checksum.c \
	plan/ws175/tests/host-deflate.c -o "$out/host-deflate"
status=0
"$out/host-deflate" "$out" > "$out/deflate.txt" 2>&1 || status=1
grep -v '^ok' "$out/deflate.txt" || true
python3 - "$out" <<'PYTHON' || status=1
import pathlib, sys, zlib
out = pathlib.Path(sys.argv[1])
bad = 0
count = 0
for packed in sorted(out.glob("level-*.z")):
	which = packed.stem.split("-")[2]
	if zlib.decompress(packed.read_bytes()) != (out / f"input-{which}.bin").read_bytes():
		print(f"FAIL the host's zlib reads {packed.name} differently")
		bad += 1
	count += 1
print(f"host zlib: {count} streams read back, {bad} different")
sys.exit(1 if bad else 0)
PYTHON
[ $status -eq 0 ] && echo "run-host-deflate: PASS" || echo "run-host-deflate: FAIL"
exit $status
