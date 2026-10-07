# ws165-p005 (and p003's T1-303): the image and the BIN of plan/ws102/tests/osk-guest.sh's install and hand steps for
# the handwriting face: the inset image (plan/ws102/tests/config-amd64-inset.mk: the WS079 demonstration image with
# the injected touch screen and pen, and Text Editor) with ime-probe, which the hand step sends the first candidate to
# (userland/tests/ime-probe; T1-303 found it missing), and Settings.  The compositor's package brings the hand-hershey
# templates.  Build:
#   plan/tools/guest/test-image.sh plan/ws165/tests/config-amd64-hand.mk BUILD \
#     --file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws102/tests/config-amd64-inset.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),ime-probe settings)
