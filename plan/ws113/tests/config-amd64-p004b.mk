# ws113-p004b: the image of the several displays checks: the p003 image (the Files image with display-events and
# display-control) with the compositor's capture channel (ZEDBSD_TEST_SCREEN_CAPTURE, whose DISPLAYS, MODE and PLACE
# requests the test sends) and keiland-shot.  QEMU (T1): plan/ws113/tests/displays-p004b.sh.
#   FILES_CONFIG=plan/ws113/tests/config-amd64-p004b.mk plan/tools/files/build-files-image.sh BUILD
include plan/ws113/tests/config-amd64-p003.mk
ZEDBSD_TEST_SCREEN_CAPTURE := y
ZEDBSD_USER_PROGRAMS += keiland-shot
