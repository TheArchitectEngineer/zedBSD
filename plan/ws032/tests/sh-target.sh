#!/bin/sh
# WS032: the shell's command language, on the target.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Each construct is typed at the console and what it produced is compared
# with what the standard says it should be, so that what is tested is the
# shell the image actually carries.
#
# A construct written across several lines is written into a file and the
# file is run, rather than typed: the console cannot be typed ahead of,
# because the line editor discards whatever arrived while it was not
# reading, and reading a script from a file is the case that matters.
#
# usage: sh-target.sh [image]
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
image=${1:-$root/build/amd64/hdd-image.img}

exec python3 "$root/plan/ws032/tests/run-target-console.py" \
	--image "$image" --timeout 400 \
	'check() { if [ "$2" = "$3" ]; then echo "ok $1"; else echo "BAD $1 [$2] [$3]"; fi; }' \
	'if true; then r=then; else r=else; fi' \
	'check if-then "$r" then' \
	'if false; then r=then; else r=else; fi' \
	'check if-else "$r" else' \
	'if false; then r=a; elif true; then r=b; else r=c; fi' \
	'check if-elif "$r" b' \
	'r=; i=0; while [ $i -lt 3 ]; do r="$r$i"; i=$((i + 1)); done' \
	'check while "$r" 012' \
	'r=; j=0; until [ $j -ge 2 ]; do r="$r$j"; j=$((j + 1)); done' \
	'check until "$r" 01' \
	'r=; for a in x y z; do r="$r$a"; done' \
	'check for "$r" xyz' \
	'r=; for a in a b c d; do if [ "$a" = b ]; then continue; fi; if [ "$a" = d ]; then break; fi; r="$r$a"; done' \
	'check for-break "$r" ac' \
	'case hello in he*) r=star ;; *) r=other ;; esac' \
	'check case-star "$r" star' \
	'case xyz in a|b|xyz) r=alt ;; *) r=other ;; esac' \
	'check case-alt "$r" alt' \
	'greet() { echo "hi $1"; return 7; }' \
	'r=$(greet world)' \
	'check function "$r" "hi world"' \
	'greet zed > /dev/null' \
	'check function-status "$?" 7' \
	'r=$({ echo a; echo b; })' \
	'check group "$r" "$(printf "a\nb")"' \
	'r=$( (echo sub) )' \
	'check subshell "$r" sub' \
	'r=no; if ! false; then r=neg; fi' \
	'check negate "$r" neg' \
	'r=$(echo one | while read l; do echo "got $l"; done)' \
	'check pipe-compound "$r" "got one"' \
	'set -- p q r' \
	'check positional-count "$#" 3' \
	'check positional-first "$1" p' \
	'shift' \
	'check positional-shift "$1" q' \
	'x=abcdef' \
	'check length "${#x}" 6' \
	'check prefix "${x#*c}" def' \
	'check prefix-long "${x##*c}" def' \
	'check suffix "${x%c*}" ab' \
	'check suffix-long "${x%%c*}" ab' \
	'p=/a/b/c.txt' \
	'check basename "${p##*/}" c.txt' \
	'check dirname "${p%/*}" /a/b' \
	'printf "x=abcdef\ncat <<EOF\nhere \$x\nEOF\n" > /tmp/h1.sh' \
	'check heredoc "$(sh /tmp/h1.sh)" "here abcdef"' \
	'printf "x=abcdef\ncat <<\"EOF\"\nliteral \$x\nEOF\n" > /tmp/h2.sh' \
	'check heredoc-quoted "$(sh /tmp/h2.sh)" "literal \$x"' \
	'echo marker > /tmp/sh-out' \
	'check redirect-file "$(cat /tmp/sh-out)" marker' \
	'check redirect-descriptor "$(echo to-stderr 2>&1 >/dev/null)" ""' \
	'check redirect-stderr "$( (echo err >&2) 2>&1 )" err' \
	'r=; for a in 1 2; do for b in x y; do if [ $b = y ]; then continue 2; fi; r="$r$a$b"; done; done' \
	'check nested-continue "$r" 1x2x' \
	'printf "if true\nthen\necho multi\nfi\n" > /tmp/m1.sh' \
	'check multiline-if "$(sh /tmp/m1.sh)" multi' \
	'printf "for a in 1 2\ndo\nprintf %%s \$a\ndone\n" > /tmp/m2.sh' \
	'check multiline-for "$(sh /tmp/m2.sh)" 12' \
	'r=start; set -e; false || r=handled; set +e' \
	'check errexit-exempt "$r" handled' \
	'rm -f /tmp/sh-out /tmp/h1.sh /tmp/h2.sh /tmp/m1.sh /tmp/m2.sh' \
	'echo SH-VERIFIED'
