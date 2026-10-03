#!/bin/sh
# ws095: the Venus guest for the input method tests, with its own runtime directory (build/ws095-run) and the lean
# image (plan/ws095/tests/build-ime-image.sh).
#
#   plan/ws095/tests/ime-guest.sh start [IMAGE]    (default build/ws095/img/hdd-image.img)
#   plan/ws095/tests/ime-guest.sh stop
#   plan/ws095/tests/ime-guest.sh run 'COMMAND'
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095-run}"
export GUEST_RUNTIME
command=${1:-start}
if [ "$command" = start ]; then
	exec plan/ws035/tests/zdesktop-guest.sh start "${2:-build/ws095/img/hdd-image.img}"
fi
exec plan/ws035/tests/zdesktop-guest.sh "$@"
