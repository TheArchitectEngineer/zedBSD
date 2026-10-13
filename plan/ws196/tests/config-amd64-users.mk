# ws196-p004 (T1): the CI guest image (config/ci/config-amd64.mk: graphical login, sessiond, Settings, account-admin,
# sudo) with root's useradd, usermod and userdel (ws196-p003), for account-admin's root form (p002) and the three
# commands.  Build (BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws196/tests/config-amd64-users.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),account-admin keiland-settings settings sessiond)
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),useradd usermod userdel)
