# ws100-p004: the Venus guest image the volume is checked on: WS099's criteria image (the graphical login with Notes,
# Settings and the generated wallpapers; kei logged in at boot) with the HD Audio driver, audiod and the test client
# audiod-feedback (built by build-volume-image.sh into build/ws100-tests/).
#   plan/ws100/tests/build-volume-image.sh [BUILD]
include plan/ws099/tests/config-amd64-criteria.mk
CONFIG_DRIVER_PCI_HDA := y
ZEDBSD_USER_PROGRAMS += audiod
ifneq ($(wildcard build/ws100-tests/audiod-feedback),)
ZEDBSD_EXTRA_INPUTS += build/ws100-tests/audiod-feedback
ZEDBSD_EXTRA_FILES += --file /usr/bin/audiod-feedback=build/ws100-tests/audiod-feedback
endif
