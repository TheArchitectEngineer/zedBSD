#!/bin/sh
#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# The desktop's operations one after another, for the time of each frame
# (frame-times.py).  Run as root on a board whose Keiland runs with
# --log-frames and whose image has aat-input (CONFIG_INPUT_TEST_INJECT):
#
#     frame-ops.sh SESSION.LOG MARKS [WIDTH HEIGHT]
#
# Before each step the number of lines SESSION.LOG has goes to MARKS as
# "NAME LINE".  The steps: idle, Terminal started from App Home (launch),
# a line typed into it (type), keys typed one at a time 200 ms apart, for
# each key's way to the screen (keys, key-latency.py), a long output (output), the wheel over it
# (scroll), the pointer over the title bars and the system bar (hover), the
# Terminal's window dragged by its title bar there and back (drag), App
# Home opened and closed twice (home), Wiseview opened and closed twice
# (wiseview), and the end.  The output's size defaults to 1920 x 1080.

set -u

log=$1
marks=$2
width=${3:-1920}
height=${4:-1080}

mark() {
	printf '%s %s\n' "$1" "$(wc -l < "$log" | tr -d ' ')" >> "$marks"
}

input() {
	aat-input "$@" > /dev/null || echo "frame-ops: aat-input $* failed" >&2
}

pause() {
	aat-input sleep "$1" > /dev/null
}

: > "$marks"
if [ ! -S /run/aat-input.sock ]; then
	aat-input start --width "$width" --height "$height" || exit 1
	sleep 3
fi

# A still desktop first: the frames a quiet desktop draws.
input move-to 960 600
mark idle
sleep 3

# Terminal from App Home: Super, its name, Enter.
mark launch
start=$(wc -l < "$log" | tr -d ' ')
input key super
pause 800
input type terminal
pause 500
input key enter
sleep 5

# The Terminal's place: its body's top-left corner (KWL MAP after the launch).
place=$(tail -n "+$start" "$log" | grep 'KWL MAP client=' | tail -1)
tx=$(echo "$place" | sed -n 's/.* x=\(-\{0,1\}[0-9]*\).*/\1/p')
ty=$(echo "$place" | sed -n 's/.* y=\(-\{0,1\}[0-9]*\).*/\1/p')
if [ -z "$tx" ] || [ -z "$ty" ]; then
	echo "frame-ops: no KWL MAP after the launch; the window is taken at 400,300" >&2
	tx=400
	ty=300
fi
echo "frame-ops: terminal at $tx,$ty"

# A line typed into it, at the usual typing speed (15 ms a character).
input click left $((tx + 200)) $((ty + 150))
pause 500
mark type
input type 'echo the quick brown fox jumps over the lazy dog 0123456789'
input key enter
sleep 1

# Keys one at a time, 200 ms apart, behind a '#' so the line does nothing.
mark keys
input type '#'
pause 200
for k in a b c d e f g h i j k l m n o p q r s t; do
	input type "$k"
	pause 200
done
input key enter
sleep 1

# A long output.
mark output
input type 'seq 1 400'
input key enter
sleep 3

# The wheel over it: up and down, a notch each 50 ms.
mark scroll
i=0
while [ $i -lt 20 ]; do
	input wheel 1
	pause 50
	i=$((i + 1))
done
i=0
while [ $i -lt 20 ]; do
	input wheel -1
	pause 50
	i=$((i + 1))
done
sleep 1

# The pointer over the Terminal's title bar (its buttons light) and the system bar.
mark hover
ybar=$((ty - 30))
x=$tx
while [ $x -lt $((tx + 900)) ] && [ $x -lt $((width - 20)) ]; do
	input move-to $x $ybar
	pause 15
	x=$((x + 12))
done
x=20
while [ $x -lt $((width - 20)) ]; do
	input move-to $x 22
	pause 15
	x=$((x + 24))
done
input move-to 960 600
sleep 1

# The window dragged by its title bar, 100 moves out and 100 back.
mark drag
input drag $((tx + 60)) $ybar $((tx + 460)) $((ybar + 150)) 100
pause 300
input drag $((tx + 460)) $((ybar + 150)) $((tx + 60)) $ybar 100
sleep 1

# App Home opened and closed, twice.
mark home
i=0
while [ $i -lt 2 ]; do
	input key super
	pause 1500
	input key super
	pause 1500
	i=$((i + 1))
done

# Wiseview opened and closed, twice.
mark wiseview
i=0
while [ $i -lt 2 ]; do
	input key super+tab
	pause 1500
	input key esc
	pause 1500
	i=$((i + 1))
done

mark end
sleep 1
