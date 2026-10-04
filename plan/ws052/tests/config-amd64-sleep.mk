# ws052-p004: the SSH guest image (plan/tools/guest/config-amd64-ssh.mk) with the test of the devices' suspend and
# resume (sleepctl) and the reader of the system's events.  A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws052/tests/config-amd64-sleep.mk BUILD
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += sleepctl systemevents
