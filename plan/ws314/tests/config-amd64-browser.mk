# WS314's QEMU Venus image for the browser over libbrowser's network thread: guest.sh's image with the browser and
# what its pages need (the roots for https, the JPEG and GIF libraries).
# Build:  plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-browser.mk build/ws314-browser-img
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws314/tests/config-amd64-guest.mk
ZEDBSD_USER_PROGRAMS += browser libbrowser ca-certificates libjpeg-compat libgif-compat
