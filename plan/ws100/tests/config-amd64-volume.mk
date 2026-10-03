# ws100-p004: the Venus guest image the volume is checked on: WS099's criteria image (the graphical login with Notes,
# Settings and the generated wallpapers; kei logged in at boot) with the HD Audio driver, audiod and the test client
# audiod-feedback (built by build-volume-image.sh into BUILD/tests/).
#   plan/ws100/tests/build-volume-image.sh [BUILD]
include plan/ws099/tests/config-amd64-criteria.mk
CONFIG_DRIVER_PCI_HDA := y
ZEDBSD_USER_PROGRAMS += audiod
ifneq ($(wildcard $(BUILD)/tests/audiod-feedback),)
ZEDBSD_EXTRA_INPUTS += $(BUILD)/tests/audiod-feedback
ZEDBSD_EXTRA_FILES += --file /usr/bin/audiod-feedback=$(BUILD)/tests/audiod-feedback
endif
