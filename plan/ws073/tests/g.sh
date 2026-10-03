#!/bin/sh
# Runs plan/tools/guest/guest.py with this WS's own guest runtime, so the
# WS073 guest runs beside the other agents' guests.
#
#   sh plan/ws073/tests/g.sh start IMAGE | wait | run CMD | put | get | stop
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
GUEST_RUNTIME=${GUEST_RUNTIME:-$root/build/ws073-run}
export GUEST_RUNTIME
exec python3 "$root/plan/tools/guest/guest.py" "$@"
