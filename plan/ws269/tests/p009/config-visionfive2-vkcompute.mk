# The VisionFive 2 test image of ws269-p009: the CI board configuration with
# libvulkan and the vkcompute test (one compute dispatch through the powervr
# executor, one line on the screen).  Not for the ordinary image.
#
# The PowerVR firmware is left out because its fetch asks for agreement to
# LICENSE.powervr.  To make a runnable image, read the license and build with
#   make ZEDBSD_CONFIG=plan/ws269/tests/p009/config-visionfive2-vkcompute.mk \
#        VKCOMPUTE_FIRMWARE=powervr-firmware BUILD=build/<dir> disk-image
# where license-agree.sh shows the license and asks yes/no on the first fetch
# (the answer is kept in the firmware cache, userland/firmware/zedbsd-firmware.mk).
include config/ci/config-visionfive2.mk

VKCOMPUTE_FIRMWARE ?=
ZEDBSD_USER_PROGRAMS := $(filter-out powervr-firmware,$(ZEDBSD_USER_PROGRAMS)) \
	libwayland-client libvulkan vkcompute $(VKCOMPUTE_FIRMWARE)
