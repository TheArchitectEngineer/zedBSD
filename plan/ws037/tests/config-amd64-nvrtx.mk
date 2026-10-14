# ws037-p004: the image for the nvrtx runs on the RTX 2070 handed to a QEMU guest by VFIO
# (plan/ws037/phase004/phase.md section 3).  The CI amd64 image (config/ci/config-amd64.mk) with:
#   - the nvrtx driver built in (CONFIG_DRIVER_PCI_NVRTX, on by default since 2026-10-15),
#   - the GSP firmware package (nvidia-gsp-firmware; its license is accepted first with
#     "yes | make firmware-licenses", as for every image with a firmware package),
#   - the text console with the kernel's lines on it (no graphical boot or login), so the
#     stage marks are on the guest's emulated VGA screen that QMP's screendump captures,
#   - the boot words of one run.  Since 2026-10-15 the display stages run by default (the GSP boot, P4m,
#     P8a, the takeover P8b showing the console, the GPU device P8c), so each run turns off what it does
#     not test: NVRTX_RUN=R0 (nvrtx.gsp=0: N0 and N1 only), R1 (nvrtx.gsp=1 nvrtx.stop=P1a), R2 (stop=P2a),
#     R3 (stop=P3), R4 (stop=P4), R5 (stop=P5), R6 (stop=P6) or R7 (nvrtx.gsp=1, every built stage up to
#     P8a: nvrtx.disp=0 nvrtx.resident=0) or R7g (R7 and nvrtx.gr=1, the golden context) or R8b (the
#     takeover of the head the GOP lit as the monitor test, nvrtx.disp.test=1, and nvrtx.resident=0) or R8c
#     (R8b with the GPU device) or R8d (R8c with the display-scanout probe: events, refresh, cursor,
#     sharing) or RA (no words: the default boot of the main image, the console kept and shown on the
#     taken-over head, the GPU device); plan/ws037/phase004 to phase006 give what each run must show.
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
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=0
endif
# ws037-p006 method A: the main image's default boot (the console kept through the GSP boot and shown on the head).
ifeq ($(NVRTX_RUN),RA)
ZEDBSD_BOOT_EXTRA_LINES :=
ZEDBSD_USER_PROGRAMS += display-inventory display-control
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
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=P4
endif
ifeq ($(NVRTX_RUN),R5)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=P5
endif
ifeq ($(NVRTX_RUN),R6)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=P6
endif
ifeq ($(NVRTX_RUN),R7)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.disp=0 nvrtx.resident=0
endif
# ws037-p005 5f: R7 with the graphics golden context and the TU10x scrubber (stage P5g).
ifeq ($(NVRTX_RUN),R7g)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.gr=1 nvrtx.disp=0 nvrtx.resident=0
endif
# ws037-p006 P8b: R7 with the takeover of the head the GOP lit (needs the RTX's option ROM run, rombar=1).
ifeq ($(NVRTX_RUN),R8b)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.disp.test=1 nvrtx.resident=0
endif
# ws037-p008 stage C: R7g with the first compute launch (stage P9c: a program the nvrtx compiler made writes 42).
ifeq ($(NVRTX_RUN),R7c)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.gr=1 nvrtx.compute=1 nvrtx.disp=0 nvrtx.resident=0
endif
# ws037-p007 7b/7c: R7c with the first draw on the compute channel (stage P9g: a quad into a system-memory and a VRAM
# target, then textured).
ifeq ($(NVRTX_RUN),R7d)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.gr=1 nvrtx.compute=1 nvrtx.draw=1 nvrtx.disp=0 nvrtx.resident=0
endif
# ws037-p006 P8c: R8b with the GSP kept up and the GPU registered as a GPU device (the default since 2026-10-15),
# and the display probes that drive it from the serial console (display-inventory, display-control).
ifeq ($(NVRTX_RUN),R8c)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.disp.test=1
ZEDBSD_USER_PROGRAMS += display-inventory display-control
endif
# ws037-p006 P8c (2): R8c's device with the display's events, refresh, cursor plane, blob sharing and scanout constraints,
# and the probe that drives the new parts from the serial console (display-scanout) besides R8c's two.
ifeq ($(NVRTX_RUN),R8d)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.disp.test=1
ZEDBSD_USER_PROGRAMS += display-inventory display-control display-scanout
endif
# ws037-p007 D1: R7 with the work channel the GPU device will keep (stage P9w: the golden context, the channel with
# the 3D and compute objects, its first fence), stopped again at the end.
ifeq ($(NVRTX_RUN),R9w)
ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.work=1 nvrtx.disp=0 nvrtx.resident=0
endif
