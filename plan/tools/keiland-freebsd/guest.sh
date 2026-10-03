#!/bin/sh
# zedBSD; Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Controls only the isolated FreeBSD 15.1 guest of WS137 (loopback SSH and QMP; see README.md).
set -eu
exec python3 "$(dirname "$0")/guest.py" "$@"
