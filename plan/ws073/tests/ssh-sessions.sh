#!/bin/sh
# BUG-051 reproduction attempt, from the host, with a guest already started
# by g.sh: starts two loops of clang compiles in the guest as load, opens N
# short SSH sessions meanwhile, and reports failed sessions and any process
# killed by a signal in the guest's /var/log/messages.
#
#   sh plan/ws073/tests/ssh-sessions.sh [N]
#
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
count=${1:-60}
here=$(cd "$(dirname "$0")" && pwd)
g="sh $here/g.sh"
$g run 'printf "int main(void){return 0;}\n" > /tmp/l.c; for k in 1 2; do (i=0; while [ $i -lt 60 ]; do clang -O1 -c /tmp/l.c -o /tmp/l$k.o; i=$((i+1)); done) > /dev/null 2>&1 & done; echo load started'
n=0
failures=0
while [ "$n" -lt "$count" ]; do
	if ! $g run true > /dev/null 2>&1; then
		failures=$((failures + 1))
	fi
	n=$((n + 1))
done
echo "sessions $n, failed $failures"
$g run 'echo "killed by a signal: $(grep -c "killed by signal" /var/log/messages)"; grep -E "killed by signal|terminated by signal" /var/log/messages | tail -5; true'
