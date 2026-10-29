#!/bin/sh
# The desktop program of guest.sh's compositor: WS314's tests one after another, then Terminal running a command.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
/tmp/post-test > /tmp/post.log 2>&1
/tmp/process-test > /tmp/process.log 2>&1
# The one copy's socket needs a runtime directory of the user's alone (the compositor gives its programs /tmp); the
# compositor's socket is reached from there by a link.
mkdir -p -m 700 /tmp/ws314inst
ln -sf "$XDG_RUNTIME_DIR/${WAYLAND_DISPLAY:-wayland-0}" "/tmp/ws314inst/${WAYLAND_DISPLAY:-wayland-0}"
XDG_RUNTIME_DIR=/tmp/ws314inst /tmp/instance-test > /tmp/instance.log 2>&1
/bin/terminal '--command=echo zterm-$((40+2)) > /tmp/zterm.txt; exit 3' --timeout-s=30 > /tmp/terminal.log 2>&1
cat /tmp/zterm.txt >> /tmp/terminal.log 2>&1
echo done > /tmp/ws314-done
