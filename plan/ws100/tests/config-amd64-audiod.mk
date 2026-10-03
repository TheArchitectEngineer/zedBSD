# ws100-p002: the userland test image (plan/ws035/tests/config-amd64-userland.mk) with the HD Audio driver, audiod and
# the test client audiod-feedback (plan/ws100/tests/audiod-feedback.c, built by build-audiod-image.sh into
# BUILD/tests/ before the image; the first build is without it).
#   plan/ws100/tests/build-audiod-image.sh [BUILD]
include plan/ws035/tests/config-amd64-userland.mk
CONFIG_DRIVER_PCI_HDA := y
CONFIG_PCAT_SERIAL_MIRROR := y
ZEDBSD_USER_PROGRAMS += audiod
ifneq ($(wildcard $(BUILD)/tests/audiod-feedback),)
ZEDBSD_EXTRA_INPUTS += $(BUILD)/tests/audiod-feedback
ZEDBSD_EXTRA_FILES += --file /usr/bin/audiod-feedback=$(BUILD)/tests/audiod-feedback
endif
