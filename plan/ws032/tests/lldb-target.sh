#!/bin/sh
# WS032 p009: the debugger, on the target.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# A debugger has worked when it has stopped a program where it was asked
# to, shown where the program was, and let it finish.  So a small program
# is compiled with debug information on the target, lldb is told to stop
# in one of its functions, and what it prints at the stop and after the
# continue is what settles the matter: the breakpoint with the arguments
# the function was called with, a backtrace naming the caller, and the
# program's own exit status at the end.
#
# usage: lldb-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 900 \
	'rm -f /root/p.c' \
	'printf "/* A function to stop in, and a result carried in the exit status. */\nstatic int add(int a, int b)\n{\n\treturn a + b;\n}\n\nint main(void)\n{\n" >> /root/p.c' \
	'printf "\treturn add(2, 3) == 5 ? 42 : 1;\n}\n" >> /root/p.c' \
	'clang -g /root/p.c -o /root/p' \
	'/root/p; echo plain=$?' \
	'lldb -b -o "breakpoint set --name add" -o "run" -o "thread backtrace" -o "continue" -- /root/p' \
	'echo LLDB-END'
