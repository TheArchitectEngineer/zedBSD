# The camera's test image: the amd64 CI configuration with camcap, which captures frames from /dev/camN
# (userland/tests/camcap).  The Latitude 5330 with an HDMI capture device or a USB webcam.
include config/ci/config-amd64.mk
ZEDBSD_USER_PROGRAMS += camcap
