# ws170-p000 (Phone's mock): the zdesktop guest image for the evening UAT and T1 (the CI image's configuration,
# config/ci/config-amd64.mk) with Phone, which App Home shows.  Build (the result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws170/tests/config-amd64-phone.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),phone)
