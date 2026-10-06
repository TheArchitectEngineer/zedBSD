#!/bin/sh
# ws172-p003: builds the image of plan/ws172/tests/fido2-p003-guest.sh: plan/ws172/tests/config-amd64-fido2.mk with the
# same files as build-passkey-image.sh (the wallpaper, the sample home maker).
#   plan/ws172/tests/build-fido2-image.sh [BUILD] [TARGET...]     (default build/ws172-fido2-image, disk-image)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws172-fido2-image}
[ $# -ge 1 ] && shift
exec plan/tools/guest/test-image.sh plan/ws172/tests/config-amd64-fido2.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh \
	"$@"
