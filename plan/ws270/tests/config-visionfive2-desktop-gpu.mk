# WS270 p006a (2b): the VisionFive 2 desktop with graphical login on the PowerVR GPU.
# The desktop programs of config-visionfive2-desktop.mk, plus the PowerVR firmware,
# gpu-selftest (run by hand, /bin/gpu-selftest) and vkmatrix.  The firmware's
# license was agreed by the user; the build reads the accepted cache under
# build/sources/firmware/powervr.
include plan/ws270/tests/config-visionfive2-desktop.mk

ZEDBSD_GRAPHICAL_LOGIN := y
ZEDBSD_USER_PROGRAMS += mount sh gpu-selftest vkmatrix powervr-firmware

# The compositor's test-only screen capture (userland/desktop/wayland/shot.c) and
# its client keiland-shot, so the board's screen can be read over SSH (Q1 2026-10-21,
# the user's 「デスクトップが動くまで自走」). A test image only.
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += keiland-shot

# The test-only input injection (/dev/input-inject) and aat-input, so the
# cursor, typing and window-drag scenarios of ws270-p007 can be run over SSH
# (Q1 2026-10-21, the user's 「描画が遅すぎて…これを解決したいです。」). A test image only.
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += aat-input
