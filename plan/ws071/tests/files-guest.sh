#!/bin/sh
# ws071: the Venus guest for the File Manager tests, with its own runtime directory
# (build/ws071-run) and the lean image (plan/ws071/tests/build-files-image.sh).
#
#   plan/ws071/tests/files-guest.sh start [IMAGE]    (default build/amd64/hdd-image.img)
#   plan/ws071/tests/files-guest.sh stop
#   plan/ws071/tests/files-guest.sh run 'COMMAND'
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
command=${1:-start}
if [ "$command" = start ]; then
	exec plan/ws035/tests/zdesktop-guest.sh start "${2:-build/amd64/hdd-image.img}"
fi
exec plan/ws035/tests/zdesktop-guest.sh "$@"
