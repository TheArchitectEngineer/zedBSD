# ws056-p002: plan/ws045/tests/config-amd64-base.mk with the console mirrored
# on the first serial port (plan/tools/guest/serial.py), for running a test
# on the console.
include plan/ws045/tests/config-amd64-base.mk
CONFIG_PCAT_SERIAL_MIRROR := y
