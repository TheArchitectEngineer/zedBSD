# ws132-p002: the SSH guest image (plan/tools/guest/config-amd64-ssh.mk) with the reader of the system's events
# (systemevents).  A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws132/tests/config-amd64-events.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += systemevents
