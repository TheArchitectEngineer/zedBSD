# ws170 (Phone): the zdesktop guest image for T1 and the UAT (the CI image's configuration, config/ci/config-amd64.mk)
# with Phone, which App Home shows, and keiland-settings (the AAT helper sets phone.backend with it).  Build (the
# result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws170/tests/config-amd64-phone.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),phone keiland-settings)
