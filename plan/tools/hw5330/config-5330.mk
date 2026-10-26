# ws245: the Latitude 5330 regression image (plan/tools/hw5330/5330-run.sh): the CI amd64 configuration with the
# compositor's test-only capture and its client keiland-shot (the screen's PNG over SSH), and backlight-probe on the
# image (the survey runs /bin/backlight-probe).  A test image only.
#   make ZEDBSD_CONFIG=plan/tools/hw5330/config-5330.mk BUILD=<dir> disk-image
include config/ci/config-amd64.mk
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += keiland-shot backlight-probe
