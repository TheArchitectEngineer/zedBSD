# ws305: the guest image for the GPU passthrough VM on the desktop (10.0.10.3, Debian booted with the "Debian VFIO"
# grub entry, QEMU q35 + OVMF with the RX 5600 XT given to the guest through vfio-pci): the same contents as the
# WS291 image (the CI amd64 image with amdrdna built in, the control tool, the Navi10 firmware, the test-only input
# injection and screen capture, mview) and amdrdna.reset=1 on the boot line, so a GPU left running by the previous
# guest is reset by the guest itself (PSP mode-1 reset and the VBIOS post, step R1 of plan/ws305/phase001/phase.md).
# Build:  plan/tools/guest/test-image.sh plan/ws305/tests/config.mk build/ws305/vm \
#             --file /etc/keiland/autologin=plan/ws291/tests/autologin
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_AMDRDNA := y
CONFIG_AMDRDNA_CONTROL := y
ZEDBSD_USER_PROGRAMS += amdrdnactl amd-navi-firmware

# The desktop's operations driven over SSH and its screen read back (test images only).
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += aat-input keiland-shot

ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),mview)

# The guest resets a GPU the previous guest left running (no FLR on Navi10).
ZEDBSD_BOOT_EXTRA_LINES := amdrdna.reset=1
