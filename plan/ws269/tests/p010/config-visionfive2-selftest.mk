# The VisionFive 2 test image of ws269-p010a and p010c (design-p010.md §8.2, D-P2): the
# CI board configuration with libvulkan, vkcompute and gpu-selftest.  Not for
# the ordinary image.
#
# GPU_SELFTEST_BOOT=y (the default) also selects gpu-selftest-boot, the
# oneshot service that runs gpu-selftest at boot with its lines on the
# console; GPU_SELFTEST_BOOT=n leaves it to be run by hand (/bin/gpu-selftest).
#
# The PowerVR firmware is left out because its fetch asks for agreement to
# LICENSE.powervr.  To make a runnable image, read the license and build with
#   make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk \
#        SELFTEST_FIRMWARE=powervr-firmware BUILD=build/<dir> disk-image
# where license-agree.sh shows the license and asks yes/no on the first fetch.
include config/ci/config-visionfive2.mk

SELFTEST_FIRMWARE ?=
GPU_SELFTEST_BOOT ?= y
ZEDBSD_USER_PROGRAMS := $(filter-out powervr-firmware,$(ZEDBSD_USER_PROGRAMS)) \
	libwayland-client libvulkan vkcompute vkimage vkstage gpu-selftest \
	$(if $(filter y,$(GPU_SELFTEST_BOOT)),gpu-selftest-boot) $(SELFTEST_FIRMWARE)
