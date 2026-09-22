#!/bin/sh
# WS032 p009: the debugger, on the target.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# A debugger has worked when it has stopped a program where it was asked
# to, shown where the program was and what it held, watched a variable
# change, let the program run on, and seen it finish.  So a small program
# is compiled with debug information on the target and put through all of
# that; what the debugger prints at each stop is what settles the matter.
#
# The commands go in a file because a console types one line at a time and
# a debugger session is longer than a line.
#
# usage: lldb-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 900 \
	'rm -f /root/p.c' \
	'printf "/* A function to stop in, a global to watch, and a result in the status. */\nstatic int total;\n\nstatic int add(int a, int b)\n{\n\tint sum;\n\n" >> /root/p.c' \
	'printf "\tsum = a + b;\n\ttotal = total + sum;\n\treturn sum;\n}\n\nint main(void)\n{\n\tint i;\n\tint result;\n\n" >> /root/p.c' \
	'printf "\tresult = 0;\n\tfor (i = 0; i < 3; i++)\n\t\tresult = result + add(i, i + 1);\n\treturn result == 9 && total == 9 ? 42 : 1;\n}\n" >> /root/p.c' \
	'clang -g /root/p.c -o /root/p' \
	'/root/p; echo plain=$?' \
	'rm -f /root/session.txt' \
	'printf "breakpoint set --name add\nrun\nbt\nframe variable\np a + b\n" >> /root/session.txt' \
	'printf "watchpoint set variable total\ncontinue\np total\n" >> /root/session.txt' \
	'printf "thread step-out\nframe info\nthread step-over\n" >> /root/session.txt' \
	'printf "watchpoint delete 1\nbreakpoint delete 1\ncontinue\n" >> /root/session.txt' \
	'lldb -b -s /root/session.txt -- /root/p' \
	'echo LLDB-END'
