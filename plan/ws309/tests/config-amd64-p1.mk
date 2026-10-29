# ws309: the QEMU Venus image for the compositor's threads: the forgery image's programs (gpu-forge-test, wltest,
# acquire-fence-test, the criteria's desktop) with the test-only screen capture (keiland-shot), mview and
# keiland-settings (a wallpaper chosen during the session).
# Build:  plan/tools/guest/test-image.sh plan/ws309/tests/config-amd64-p1.mk build/ws309/img \
#             --file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/gpu-boundary/config-amd64-forge.mk

ZEDBSD_TEST_SCREEN_CAPTURE := y
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),keiland-shot mview aat-input keiland-settings)
