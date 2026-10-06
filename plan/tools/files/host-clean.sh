#!/bin/sh
# ws071: removes the sample homes and clipboards that host-run.sh made (build/ws071-host/home, run.*, current).
#
#   sh plan/tools/files/host-clean.sh WORKTREE
#
# Q1 runs this (2026-10-06 user: rm is run by Q1's pipeline; subagents do not run rm).  Only those paths under
# WORKTREE/build/ws071-host are removed; files-render and the rest stay.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
[ $# -eq 1 ] || { echo "usage: host-clean.sh WORKTREE" >&2; exit 2; }
host=$(cd -- "$1" && pwd)/build/ws071-host
[ -d "$host" ] || exit 0
rm -rf -- "$host/home" "$host"/run.*
rm -f -- "$host/files.clipboard" "$host/current"
