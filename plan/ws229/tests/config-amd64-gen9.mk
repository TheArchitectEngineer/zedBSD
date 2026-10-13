# ws229-p005: the USB image that boots a Gen9 or Gen9.5 laptop (6th, 7th and 8th generation Core) by itself and runs
# every i915 test a Gen9 laptop is judged by in one boot.  The CI amd64 image (config/ci/config-amd64.mk) with:
#   - i915.gen9=on (ws229-p002's boot parameter; the driver leaves a Gen9 GPU alone without it until the hardware PASS),
#   - the text console with the kernel's lines on it (no graphical boot, no graphical login: Gen9 has no display
#     driver yet and the firmware's framebuffer stays the console; a photo of the screen is the fallback record),
#   - the i915 test build of the scenario "gen9" (test set gen9, platform/amd64/vmunix.mk): eu, then vkcs, vkx, vke1,
#     vke2 and vkc one after another, a summary line for each and one verdict line
#     (src/drivers/gpu/i915/tests/execution/sequence.c).
# sshd, the kei account and DHCP on a USB LAN adapter come from the CI image; plan/ws229/tests/gen9-hw.md is the
# procedure and plan/ws229/tests/gen9-images.sh builds the image:
#   plan/tools/guest/test-image.sh --no-harness plan/ws229/tests/config-amd64-gen9.mk build/ws229-gen9 disk-image
include config/ci/config-amd64.mk

ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_GRAPHICAL_LOGIN := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y
ZEDBSD_BOOT_EXTRA_LINES := i915.gen9=on

I915_TESTS := y
I915_TEST_SET := gen9
ZEDBSD_TEST_CPPFLAGS := -DI915_TEST_SCENARIO=gen9
