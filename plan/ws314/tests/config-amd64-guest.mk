# WS314's QEMU Venus image: the menu image's programs (the compositor, Terminal and the probes), Settings, and Files
# with Text Editor (a file opened apart from Files).
# Build:  plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-guest.mk build/ws314-img
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/titlebar/config-amd64-menu.mk
ZEDBSD_USER_PROGRAMS += settings
ZEDBSD_USER_PROGRAMS += files textedit
