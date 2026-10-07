# ws113-p006: the image of the Settings Display page checks: the p005 image (two heads, the system extension's probe
# keiland-system) with Settings.  QEMU (T1): plan/ws113/tests/displays-p006.sh.
#   FILES_CONFIG=plan/ws113/tests/config-amd64-p006.mk plan/tools/files/build-files-image.sh BUILD
include plan/ws113/tests/config-amd64-p005.mk
ZEDBSD_USER_PROGRAMS += settings
