#!/bin/sh
# The desktop program of guest.sh's compositor: WS314's tests one after another, Terminal running a command, then a
# Terminal (t1) for guest.sh to ask for a new window, and Files on a folder of a text and a picture once guest.sh
# asks for it.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
/tmp/post-test > /tmp/post.log 2>&1
/tmp/process-test > /tmp/process.log 2>&1
/tmp/launch-test > /tmp/launch.log 2>&1
/tmp/language-test > /tmp/language.log 2>&1
# The one copy's socket needs a runtime directory of the user's alone (the compositor gives its programs /tmp); the
# compositor's socket is reached from there by a link.
mkdir -p -m 700 /tmp/ws314inst
ln -sf "$XDG_RUNTIME_DIR/${WAYLAND_DISPLAY:-wayland-0}" "/tmp/ws314inst/${WAYLAND_DISPLAY:-wayland-0}"
XDG_RUNTIME_DIR=/tmp/ws314inst /tmp/instance-test > /tmp/instance.log 2>&1
/bin/terminal '--command=echo zterm-$((40+2)) > /tmp/zterm.txt; exit 3' --timeout-s=30 > /tmp/terminal.log 2>&1
cat /tmp/zterm.txt >> /tmp/terminal.log 2>&1
/bin/terminal --token=t1 --timeout-s=200 > /tmp/t1.log 2>&1 &
echo done > /tmp/ws314-done
i=0
while [ ! -e /tmp/ws314-files ] && [ $i -lt 240 ]; do sleep 1; i=$((i+1)); done
mkdir -p /tmp/ws314demo /tmp/fhome
printf 'Notes\n' > /tmp/ws314demo/notes.txt
cp /tmp/ws314-picture.png /tmp/ws314demo/picture.png
HOME=/tmp/fhome /bin/files --token=f1 --timeout-s=120 --width=1000 --height=640 /tmp/ws314demo > /tmp/files.log 2>&1
