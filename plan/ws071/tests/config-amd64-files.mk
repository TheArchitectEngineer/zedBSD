# ws071 (File Manager): a lean zdesktop guest image for the Venus tests, the ws070
# System Menu image (plan/ws070/tests/config-amd64-menu.mk) with zdesktop-files.
# Build:
#   plan/ws071/tests/build-files-image.sh [BUILD]
include plan/ws070/tests/config-amd64-menu.mk
ZEDBSD_USER_PROGRAMS += libz-compat libpng-compat zdesktop-files
