# ws229-p005: the USB image that boots a Gen9 or Gen9.5 laptop (6th, 7th and 8th generation Core) by itself and runs
# one i915 test scenario after the GPU starts.  The CI amd64 image (config/ci/config-amd64.mk) with:
#   - i915.gen9=on (ws229-p002's boot parameter; the driver leaves a Gen9 GPU alone without it until the hardware PASS),
#   - the text console with the kernel's lines on it (no graphical boot, no graphical login: Gen9 has no display
#     driver yet and the firmware's framebuffer stays the console; a photo of the screen is the fallback record),
#   - the i915 test build of one scenario, GEN9_SCENARIO (eu, vkcs, vkx, vke1, vke2 or vkc), with the test set that
#     holds it (platform/amd64/vmunix.mk: no test kernel holds every scenario).
# sshd, the kei account and DHCP on a USB LAN adapter come from the CI image; plan/ws229/tests/gen9-hw.md is the
# procedure.  Build one image per scenario, each into its own BUILD:
#   plan/tools/guest/test-image.sh --no-harness plan/ws229/tests/config-amd64-gen9.mk build/ws229-gen9/eu GEN9_SCENARIO=eu
# (plan/ws229/tests/gen9-images.sh builds the six.)
include config/ci/config-amd64.mk

ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_GRAPHICAL_LOGIN := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y
ZEDBSD_BOOT_EXTRA_LINES := i915.gen9=on

GEN9_SCENARIO ?= eu
I915_TESTS := y
I915_TEST_SET := $(GEN9_SCENARIO)
ifeq ($(GEN9_SCENARIO),eu)
I915_TEST_SET := execution
endif
ifeq ($(GEN9_SCENARIO),vkcs)
I915_TEST_SET := compute
endif
ZEDBSD_TEST_CPPFLAGS := -DI915_TEST_SCENARIO=$(GEN9_SCENARIO)
