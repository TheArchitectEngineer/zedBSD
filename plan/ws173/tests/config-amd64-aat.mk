# ws173-p001, p002: the image of the AAT interfaces' QEMU check: the lean menu image (plan/tools/titlebar/config-amd64-menu.mk,
# with seat-probe) and the AAT's test parts: the injector (CONFIG_INPUT_TEST_INJECT, /dev/input-inject's mouse and keyboard),
# the compositor's screen capture (ZEDBSD_TEST_SCREEN_CAPTURE, userland/desktop/wayland/shot.c), aat-input and keiland-shot.
# A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws173/tests/config-amd64-aat.mk BUILD \
#	--file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png
include plan/tools/titlebar/config-amd64-menu.mk
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += aat-input keiland-shot
