# ws037-p004: the image for the nvrtx runs on the RTX 2070 handed to a QEMU guest by VFIO
# (plan/ws037/phase004/phase.md section 3).  The CI amd64 image (config/ci/config-amd64.mk) with:
#   - the nvrtx driver built in (CONFIG_DRIVER_PCI_NVRTX, off by default),
#   - the GSP firmware package (nvidia-gsp-firmware; its license is accepted first with
#     "yes | make firmware-licenses", as for every image with a firmware package),
#   - the text console with the kernel's lines on it (no graphical boot or login), so the
#     stage marks are on the guest's emulated VGA screen that QMP's screendump captures,
#   - the boot words of one run: NVRTX_RUN=R0 (no words: N0 and N1 only), R1 (nvrtx.gsp=1
#     nvrtx.stop=P1a), R2 (stop=P2a), R3 (stop=P3) or R4 (nvrtx.gsp=1, every built stage).
# Build one image per run, each into its own BUILD:
#   plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r0 NVRTX_RUN=R0
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_NVRTX := y
ZEDBSD_USER_PROGRAMS += nvidia-gsp-firmware

ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_GRAPHICAL_LOGIN := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y

NVRTX_RUN ?= R0
ifeq ($(NVRTX_RUN),R0)
ZEDBSD_BOOT_EXTRA_LINES :=
endif
ifeq ($(NVRTX_RUN),R1)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=P1a
endif
ifeq ($(NVRTX_RUN),R2)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=P2a
endif
ifeq ($(NVRTX_RUN),R3)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=P3
endif
ifeq ($(NVRTX_RUN),R4)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1
endif
