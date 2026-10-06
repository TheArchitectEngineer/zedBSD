# ws145 (printing): the AAT image (plan/tools/aat/config-amd64-aat.mk) with the printer daemon and the test client.
# Build (the result is BUILD/hdd-image.img):
#   plan/tools/guest/test-image.sh plan/ws145/tests/config-amd64-print.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),keiland-printd)
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),printtest)
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),settings)
