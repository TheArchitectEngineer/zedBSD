#!/bin/sh
# WS032 p008: the compiler, on the target.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The compiler was cross-built, so the only question that settles whether
# it works is whether it compiles something on the machine it was built
# for, and whether what it produced then runs there.  Both halves are
# checked: a compiler that emits an object nothing can run has not
# compiled anything.
#
# usage: clang-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 400 \
	'clang --version' \
	'printf "#include <stdio.h>\nint main(void){ printf(\"HELLO-FROM-CLANG\\\\n\"); return 0; }\n" > /tmp/hello.c' \
	'cat /tmp/hello.c' \
	'clang /tmp/hello.c -o /tmp/hello' \
	'/tmp/hello' \
	'printf "int main(void){ return 42; }\n" > /tmp/status.c' \
	'clang /tmp/status.c -o /tmp/status && /tmp/status; echo "status $?"' \
	'echo CLANG-VERIFIED'
