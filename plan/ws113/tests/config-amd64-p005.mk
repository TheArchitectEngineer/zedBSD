# ws113-p005: the image of the displays' system extension checks: the p004b image with the system extension's probe
# (keiland-system, whose displays, display-mode, display-place and brightness commands the test runs).
# QEMU (T1): plan/ws113/tests/displays-p005.sh.
#   FILES_CONFIG=plan/ws113/tests/config-amd64-p005.mk plan/tools/files/build-files-image.sh BUILD
include plan/ws113/tests/config-amd64-p004b.mk
ZEDBSD_USER_PROGRAMS += keiland-system
