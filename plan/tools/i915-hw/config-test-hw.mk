# ws075: the image of the i915 test build on the passthrough of the 5330 (plan/ws031/tests/vkloop-hw.sh test <scenario>,
# e.g. vkx).  The zdesktop image's configuration (plan/ws031/tests/config-zdesktop-hw.mk) with the kernel's console
# mirrored to the serial port, which vkloop-hw.sh reads the scenario's verdict lines from.
include plan/ws031/tests/config-zdesktop-hw.mk
CONFIG_PCAT_SERIAL_MIRROR := y
