#!/bin/sh
# BUG-095: the SSH guest image with a oneshot service that powers the guest off (bug095_poweroff) and its rc.conf.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/wallpapers/Birch-Lake.png.
#
#   plan/ws099/tests/bug095/build-bug095-image.sh BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
build=${1:?usage: build-bug095-image.sh BUILD}
exec plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-ssh.mk "$build" \
	--file /etc/service.d/bug095_poweroff=plan/ws099/tests/bug095/bug095_poweroff \
	--file /etc/bug095-poweroff.sh=plan/ws099/tests/bug095/bug095-poweroff.sh \
	ZEDBSD_TEST_RC_CONF=plan/ws099/tests/bug095/rc.conf
