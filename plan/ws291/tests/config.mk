# ws291: the image for the desktop's and the 3D scene's speed on the RX 5600 XT machine (10.0.10.3, zedBSD started
# from the USB stick once with UEFI BootNext): the CI amd64 image (amdrdna built in, graphical login) with the
# control tool, the Navi10 firmware, the test-only input injection and screen capture (frame-ops, keiland-shot),
# and mview.  Its kernel and files replace the stick's one by one (Q1 2026-10-26: no whole-stick write).
# Build:  plan/tools/aat/build-image.sh-like, see plan/ws291/phase001/phase.md
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_AMDRDNA := y
CONFIG_AMDRDNA_CONTROL := y
ZEDBSD_USER_PROGRAMS += amdrdnactl amd-navi-firmware

# The desktop's operations driven over SSH (plan/tools/frame-ops/) and its screen read back (test images only).
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += aat-input keiland-shot

# The 3D scene (ws291-p002).
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),mview)
