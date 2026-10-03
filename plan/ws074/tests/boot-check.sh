#!/bin/sh
# ws074: the end-of-phase boot test: rebuilds the browser guest image, boots it with
# plan/tools/boot-test.sh and copies the login screen to the shared screenshot directory.
#
#   sh plan/ws074/tests/boot-check.sh PHASE        (e.g. p004)
#
# The picture goes to build/ws074-boot-PHASE/login.png and, as PHASE-DATE-boot-login.png, to build/ws074-shots/
# (SHOTS overrides it; ws136-p003: it was a fixed other tree, now gone).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
phase=$1
sh plan/ws074/tests/build-browser-image.sh > "build/$phase-image.log" 2>&1
OUTPUT=build/ws074-boot-$phase bash plan/tools/boot-test.sh build/amd64/hdd-image.img
shots=${SHOTS:-build/ws074-shots}
mkdir -p "$shots"
cp "build/ws074-boot-$phase/login.png" "$shots/$phase-$(date +%Y%m%d)-boot-login.png"
echo "boot-check: $shots/$phase-$(date +%Y%m%d)-boot-login.png"
