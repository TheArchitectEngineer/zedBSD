# WS307 p002: the image of the OS name's checks (plan/ws307/tests/p002-names.sh): the AAT image (the UAT
# configuration with the test input and the screen's capture), so the script boots it, photographs the boot screen
# and the console's login prompt, and logs in as user/user on the Venus guest.  The development environment (clang,
# libc++) and Emacs stay out: the checks do not use them, and the toolchain's packages are not built for a test.
#   AAT_CONFIG=plan/ws307/tests/config-amd64-p002.mk plan/tools/aat/build-image.sh build/ws307-p002
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS := $(filter-out clang libcxx emacs,$(ZEDBSD_USER_PROGRAMS))
