# ws076: the lean amd64 image (plan/ws045/tests/config-amd64-base.mk) with
# the console mirrored on the serial port, so plan/tools/libm/guest-test.sh
# can run the libm test runner in the guest through plan/tools/guest.
include plan/ws045/tests/config-amd64-base.mk
CONFIG_PCAT_SERIAL_MIRROR := y
