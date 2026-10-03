# ws081-p016: the guest image the touch recorder is checked on: the pen image (plan/ws079/tests/config-amd64-pen.mk:
# the lean Venus image with the test injector, touchinject and peninject) with touchlog in /usr/bin (built by
# build-touchlog.sh into BUILD/tests/).  A test image only.
include plan/ws079/tests/config-amd64-pen.mk
ifneq ($(wildcard $(BUILD)/tests/touchlog),)
ZEDBSD_EXTRA_INPUTS += $(BUILD)/tests/touchlog
ZEDBSD_EXTRA_FILES += --file /usr/bin/touchlog=$(BUILD)/tests/touchlog
endif
