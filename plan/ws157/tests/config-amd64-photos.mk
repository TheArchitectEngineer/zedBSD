# ws157-p003 (Photos): the AAT image (plan/tools/aat/config-amd64-aat.mk: the UAT image with the test input and the
# screen's capture) with Photos, which App Home shows.
# Build (the result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws157/tests/config-amd64-photos.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),photos)
