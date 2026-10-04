# ws113-p012: the image of the display control checks: the Files image (plan/tools/files/config-amd64-files.mk) with the
# display control probe (display-control) and the backlight probe of p013.  QEMU (T1): plan/ws113/tests/display-control-p012.sh.
#   FILES_CONFIG=plan/ws113/tests/config-amd64-p012.mk plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += display-control backlight-probe
