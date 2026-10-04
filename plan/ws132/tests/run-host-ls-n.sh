#!/bin/sh
# ws132-p004 (T1-137): host test of ls -n (userland/base/ls/main.c), the POSIX long format with the owner's and the
# group's numbers: builds ls on the host and compares -n with -l on a file owned by an id without a name.
#   sh plan/ws132/tests/run-host-ls-n.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu17 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -D_GNU_SOURCE -I. userland/base/ls/main.c userland/base/common/command.c -o "$work/ls"
mkdir "$work/d"
printf 'x\n' > "$work/d/file"
status=0
uid=$(id -u)
gid=$(id -g)
"$work/ls" -n "$work/d" > "$work/n.txt"
"$work/ls" -l "$work/d" > "$work/l.txt"
if grep -Eq "^-[rwx-]{9} +1 +$uid +$gid +2 .* file\$" "$work/n.txt"; then echo "ok: -n shows the numbers $uid $gid"; else echo "FAIL: -n: $(cat "$work/n.txt")"; status=1; fi
name=$(id -un)
if [ "$name" != "$uid" ] && grep -q " $name " "$work/l.txt"; then echo "ok: -l still shows the name $name"; else echo "FAIL: -l: $(cat "$work/l.txt")"; status=1; fi
"$work/ls" -n "$work/d/file" > "$work/n1.txt"
if grep -Eq " $uid +$gid " "$work/n1.txt"; then echo "ok: -n of a file operand"; else echo "FAIL: -n of a file operand"; status=1; fi
[ $status = 0 ] && echo "run-host-ls-n: PASS" || echo "run-host-ls-n: FAIL"
exit $status
