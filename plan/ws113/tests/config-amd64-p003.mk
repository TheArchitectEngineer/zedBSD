# ws113-p003: the image of the Vulkan display events checks: the Files image (plan/tools/files/config-amd64-files.mk) with the
# display events probe (display-events) and the native display control probe of p012.  QEMU (T1): plan/ws113/tests/display-events-p003.sh.
#   FILES_CONFIG=plan/ws113/tests/config-amd64-p003.mk plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += display-events display-control
