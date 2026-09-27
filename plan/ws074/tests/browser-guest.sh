#!/bin/sh
# ws074: the guest for the browser tests, with its own runtime directory (build/ws074-run).
#
#   plan/ws074/tests/browser-guest.sh start [IMAGE]     the Venus guest (zdesktop can run)
#   plan/ws074/tests/browser-guest.sh plain [IMAGE]     a guest without a GPU (headless modes only)
#   plan/ws074/tests/browser-guest.sh wait | stop | run 'COMMAND' | put | get | screenshot ...
#
# IMAGE defaults to build/amd64/hdd-image.img (plan/ws074/tests/build-browser-image.sh).
# The Venus guest is plan/ws035/tests/zdesktop-guest.sh's.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
command=${1:-start}
case $command in
start)
	exec plan/ws035/tests/zdesktop-guest.sh start "${2:-build/amd64/hdd-image.img}"
	;;
plain)
	exec python3 plan/tools/guest/guest.py start "${2:-build/amd64/hdd-image.img}"
	;;
*)
	exec python3 plan/tools/guest/guest.py "$@"
	;;
esac
