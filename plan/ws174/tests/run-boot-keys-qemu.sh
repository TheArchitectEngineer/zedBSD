#!/bin/sh
# ws174-p003: the boot keys' QEMU cells C0 to C4 (run by the test runner T1, one QEMU at a time).
# Build the image first:
#   plan/tools/guest/test-image.sh plan/ws174/tests/config-amd64-keys.mk build/ws174-keys
# then
#   plan/ws174/tests/run-boot-keys-qemu.sh build/ws174-keys/hdd-image.img OUTDIR [--cells C0,C1,...] [--no-usb-kbd] [--ctrl-alone]
# The verdict reads screendumps (PNG) and SSH only; see run-boot-keys-qemu.py.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
exec python3 plan/ws174/tests/run-boot-keys-qemu.py "$@"
