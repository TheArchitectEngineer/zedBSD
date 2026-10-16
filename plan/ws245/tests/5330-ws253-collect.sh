#!/bin/sh
# ws245 (for WS253, the start time): read-only, after the session settled in a 5330 bare start (5330-bootcycle.sh EXTRA).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR'
r() { echo "### $1"; sshpass -p kei ssh $SO kei@10.0.30.3 "echo kei | sudo -S sh -c '$1'" 2>/dev/null; }
r "cat /var/log/init.log"
r "tail -30 /var/log/sessiond.log"
r "grep \"KWL STARTUP\\|KWL MODE\" /run/user/1000/session.log"
r "dmesg | grep -E \"boot: |usb-storage|vfs: \""
r "sysctl hw.diskstats"
