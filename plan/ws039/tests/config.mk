# The image for the amdrdna runs on the RX 5600 XT machine (10.0.10.3, started from USB): the CI amd64 image
# (config/ci/config-amd64.mk, amdrdna built in) with the text console carrying the kernel's lines (the stage marks
# stay on the firmware's screen), the control device and its tool, and the boot words of one run.
#   AMDRDNA_RUN=A1v (default: the stages that only read) or A2, A3, ... as the stages come.
#   The firmware's mode stays the monitor's (no video= line), so the console and the stage marks are 1920x1080.
# Build:  plan/tools/guest/test-image.sh plan/ws039/tests/config.mk build/p3/usb AMDRDNA_RUN=A1v
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_AMDRDNA := y
CONFIG_AMDRDNA_CONTROL := y
ZEDBSD_USER_PROGRAMS += amdrdnactl

# The probes of the display the driver takes over (amdrdna.disp.own=1): claim, present (copied and shared), cursor,
# refresh and power.
ZEDBSD_USER_PROGRAMS += display-scanout display-control

# The Navi10 firmware the security processor loads in stage A3 (/lib/firmware/amdgpu/); its license is accepted
# first with "yes | make firmware-licenses ZEDBSD_CONFIG=plan/ws039/tests/config.mk".
ZEDBSD_USER_PROGRAMS += amd-navi-firmware

# The firmware's screen stays at the monitor's mode (2026-10-16 user: "video=640x480 を外す"): the graphical boot is
# the one switch of the image build that leaves video= out of zedbsd.cfg (the loader then keeps or asks for 1920x1080);
# the kernel's lines stay on the screen (kmsg is not quiet, the console paints over the logo) and the login stays the
# console's.
ZEDBSD_GRAPHICAL_BOOT := y
ZEDBSD_GRAPHICAL_LOGIN := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y

AMDRDNA_RUN ?= A1v
ZEDBSD_BOOT_EXTRA_LINES := amdrdna.auto=$(AMDRDNA_RUN)
