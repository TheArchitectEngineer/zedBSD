# ws089-p026 (the Users page's administration, account-admin): the zdesktop guest image for T1 (the CI image's
# configuration, config/ci/config-amd64.mk) with account-admin and the settings tool.  Build (BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws089/tests/config-amd64-account-admin.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),account-admin keiland-settings)
