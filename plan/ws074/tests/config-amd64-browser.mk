# ws074 (Web browser): the guest image for the browser tests, the ws071 File Manager image
# (zdesktop, System Menu, fonts) with zdesktop-browser.
# Build:
#   plan/ws074/tests/build-browser-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += zdesktop-browser
