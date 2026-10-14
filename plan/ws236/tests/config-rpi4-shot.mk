# ws236-p011: the Pi 4's CI config with the compositor's test-only screen capture (ZEDBSD_TEST_SCREEN_CAPTURE,
# userland/desktop/wayland/shot.c) and its client keiland-shot, so a Pi test can take a PNG of what is shown.
# A test image only.  Build the compositor and the client with it, and put them over the CI image's for the test
# (the compositor at the CI image's place of /bin/wayland, keiland-shot as /bin/keiland-shot), then put the CI
# compositor back:
#   make -j16 BUILD=build/<T>/rpi4-shot ZEDBSD_CONFIG=plan/ws236/tests/config-rpi4-shot.mk \
#     build/<T>/rpi4-shot/bin/wayland build/<T>/rpi4-shot/bin/keiland-shot
include config/ci/config-rpi4.mk
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += keiland-shot
