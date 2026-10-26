#!/bin/sh
# ws245 (for WS253, the start time): read-only, right after the first ssh answer of a 5330 bare start (5330-bootcycle.sh EARLY).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR'
echo "### time dd if=/usr/share/fonts/keiland-emoji.ttf of=/dev/null bs=1048576 (right after the first ssh answer)"
sshpass -p kei ssh $SO kei@10.0.30.3 'time dd if=/usr/share/fonts/keiland-emoji.ttf of=/dev/null bs=1048576' 2>&1
