# WS307 p015: the image of the screen lock's checks (plan/ws307/tests/p015-lock.sh): the AAT image (the UAT
# configuration with the test input and the screen's capture), so the script reads the lock's setting, waits without
# input and photographs the screen over SSH.  The development environment (clang, libc++) and Emacs stay out: the
# checks do not use them, and the toolchain's packages are not built for a test.
#   AAT_CONFIG=plan/ws307/tests/config-amd64-p015.mk plan/tools/aat/build-image.sh build/ws307-p015
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS := $(filter-out clang libcxx emacs,$(ZEDBSD_USER_PROGRAMS))
