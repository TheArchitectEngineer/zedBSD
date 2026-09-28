#!/bin/sh
# ws079: the Venus guest for the pen tests, with its own runtime directory
# (build/ws079-run) and the pen image (plan/ws079/tests/build-pen-image.sh).
#
#   plan/ws079/tests/pen-guest.sh start [IMAGE]    (default build/amd64/hdd-image.img)
#   plan/ws079/tests/pen-guest.sh wait | stop | run 'COMMAND' | put FILE DEST | get SRC FILE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-run}"
export GUEST_RUNTIME
command=${1:-start}
if [ "$command" = start ]; then
	exec plan/ws035/tests/zdesktop-guest.sh start "${2:-build/amd64/hdd-image.img}"
fi
exec plan/ws035/tests/zdesktop-guest.sh "$@"
