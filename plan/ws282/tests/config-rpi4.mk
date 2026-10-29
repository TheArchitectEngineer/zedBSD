# The camera's test image for the rpi4 board: its CI configuration with camcap and camcheck
# (userland/tests/camcap, userland/tests/camcheck), an HDMI capture device or a USB webcam on its USB.
include config/ci/config-rpi4.mk
ZEDBSD_USER_PROGRAMS += camcap camcheck
