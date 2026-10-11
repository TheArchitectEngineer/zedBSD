#!/bin/sh
# BUG-274 on the guest: ps's args is the whole command line (cut to 255
# bytes), its comm is argv[0].  Starts two sleeps, reads them by pid, kills
# them.  Copy it to the guest and run it there:
#   plan/tools/guest/guest.sh put plan/bugs/BUG-274/ps-args-guest.sh /tmp/ps-args-guest.sh
#   plan/tools/guest/guest.sh run '/bin/sh /tmp/ps-args-guest.sh'
# The last line is "ps-args-guest: PASS" or "ps-args-guest: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
status=0

# A short line: comm is the program, args the program and its arguments.
sleep 60 abc def &
short=$!
comm=$(ps -p "$short" -o comm=)
args=$(ps -p "$short" -o args=)
echo "short: comm=[$comm] args=[$args]"
case $comm in
*sleep) ;;
*) echo "short: comm is not sleep"; status=1 ;;
esac
case $args in
*"sleep 60 abc def") ;;
*) echo "short: args lacks the arguments"; status=1 ;;
esac

# A line longer than the field: args is cut to 255 bytes.
long=
count=0
while [ $count -lt 300 ]; do
	long="${long}x"
	count=$((count + 1))
done
sleep 60 "$long" &
cut=$!
args=$(ps -p "$cut" -o args=)
length=$(printf '%s' "$args" | wc -c | tr -d ' ')
echo "long: args length=$length"
if [ "$length" != 255 ]; then
	echo "long: args is not cut to 255 bytes"
	status=1
fi

kill "$short" "$cut" 2>/dev/null
wait 2>/dev/null
if [ $status = 0 ]; then
	echo "ps-args-guest: PASS"
else
	echo "ps-args-guest: FAIL"
fi
exit $status
