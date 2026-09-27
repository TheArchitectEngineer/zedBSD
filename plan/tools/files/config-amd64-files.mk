# ws071 (File Manager): a lean zdesktop guest image for the Venus tests, the ws070
# System Menu image (plan/tools/titlebar/config-amd64-menu.mk) with zdesktop-files.
# Build:
#   plan/tools/files/build-files-image.sh [BUILD]
include plan/tools/titlebar/config-amd64-menu.mk
ZEDBSD_USER_PROGRAMS += libz-compat libpng-compat zdesktop-files
