# ws132-p004 (and p005): the Files image (plan/tools/files/config-amd64-files.mk, the Venus guest: /dev/gpu0 is the seat's
# display) with volumed, its probe volumectl, and su and mount for the test's other user and the mount options.
#   FILES_CONFIG=plan/ws132/tests/config-amd64-p004.mk plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += volumed volumectl su mount
