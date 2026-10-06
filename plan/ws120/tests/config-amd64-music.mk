# ws120-p009 (Music): the AAT image (plan/tools/aat/config-amd64-aat.mk: the UAT image, the release's configuration
# with libavcodec and audiod, with the test input and the screen's capture) with Music, which App Home shows.
# Build (the result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws120/tests/config-amd64-music.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),music)
