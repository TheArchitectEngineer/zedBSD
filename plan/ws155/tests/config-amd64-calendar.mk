# ws155-p000 (Calendar's mock): the zdesktop guest image for T1 and the UAT (the CI image's configuration,
# config/ci/config-amd64.mk) with Calendar, which App Home shows.  Build (the result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws155/tests/config-amd64-calendar.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),calendar)
