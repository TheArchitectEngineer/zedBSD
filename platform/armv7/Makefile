# ARMv7-A build composition: the board's addresses, then the kernel and
# the user runtime.
include platform/armv7/toolchain.mk
include platform/armv7/$(ZEDBSD_BOARD).mk
include platform/armv7/vmunix.mk
include platform/armv7/userland.mk
include platform/armv7/rootfs.mk
include platform/armv7/disk-image.mk
include platform/armv7/run.mk
