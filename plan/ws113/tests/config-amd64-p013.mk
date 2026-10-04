# ws113-p013: the image of the backlight checks: the Files image (plan/tools/files/config-amd64-files.mk) with the
# backlight probe (backlight-probe).  QEMU (T1): plan/ws113/tests/backlight-p013.sh; the machine (5330):
# backlight-probe, backlight-probe 30, backlight-probe 100 with the user watching the panel.
#   FILES_CONFIG=plan/ws113/tests/config-amd64-p013.mk plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += backlight-probe
