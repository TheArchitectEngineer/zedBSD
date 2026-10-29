#!/bin/sh
# The desktop program of guest-browser.sh's compositor: the browser on the host's first page (QEMU's user network
# reaches the host at 10.0.2.2).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
/bin/browser --width=900 --height=640 "http://10.0.2.2:$(cat /tmp/ws314-port)/first.html" > /tmp/browser.log 2>&1
