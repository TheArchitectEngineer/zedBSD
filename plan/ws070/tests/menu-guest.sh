#!/bin/sh
# ws070: the Venus guest for the System Menu tests, with its own runtime directory
# (build/ws070-run) and the lean image (plan/ws070/tests/build-menu-image.sh).
#
#   plan/ws070/tests/menu-guest.sh start [IMAGE]    (default build/amd64/hdd-image.img)
#   plan/ws070/tests/menu-guest.sh stop
#   plan/ws070/tests/menu-guest.sh run 'COMMAND'
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws070-run}"
export GUEST_RUNTIME
command=${1:-start}
if [ "$command" = start ]; then
	exec plan/ws035/tests/zdesktop-guest.sh start "${2:-build/amd64/hdd-image.img}"
fi
exec plan/ws035/tests/zdesktop-guest.sh "$@"
