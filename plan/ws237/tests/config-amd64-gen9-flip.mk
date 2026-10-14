# ws237-p002: the CI amd64 image (config/ci/config-amd64.mk, graphical boot and graphical login as released) with the
# Gen9 flip display asked for on the boot line: i915.gen9.display=flip (the default is copy until the SV7 check
# passes; display/gen9-flip.h I915_GEN9_DISPLAY_FLIP_DEFAULT).  No login=graphical here: the CI image already boots
# to the graphical login, and a second login= line hangs the boot.
#   plan/tools/guest/test-image.sh --no-harness plan/ws237/tests/config-amd64-gen9-flip.mk build/ws237-flip
# The same image boots the SV7 passthrough (plan/tools/hwsv7/, I915_MACHINE=sv7) and the SV7 from USB.
include config/ci/config-amd64.mk

ZEDBSD_BOOT_EXTRA_LINES := i915.gen9.display=flip
