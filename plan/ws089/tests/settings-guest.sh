#!/bin/sh
# ws089: the Venus guest for the Settings tests, with its own runtime directory (build/ws089-run)
# and the lean image (plan/ws089/tests/build-settings-image.sh).
#
#   plan/ws089/tests/settings-guest.sh start [IMAGE]    (default build/amd64/hdd-image.img)
#   plan/ws089/tests/settings-guest.sh stop
#   plan/ws089/tests/settings-guest.sh run 'COMMAND'
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
command=${1:-start}
if [ "$command" = start ]; then
	exec plan/ws035/tests/zdesktop-guest.sh start "${2:-build/amd64/hdd-image.img}"
fi
exec plan/ws035/tests/zdesktop-guest.sh "$@"
