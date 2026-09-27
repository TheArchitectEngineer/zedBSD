# ws074 (Web browser): the guest image for the browser tests, the ws071 File Manager image
# (zdesktop, System Menu, fonts) with browser.
# Build:
#   plan/ws074/tests/build-browser-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += browser
# https (ws074-p017): the roots at /etc/ssl/cert.pem (the OpenSSL package is in the menu image already).
ZEDBSD_USER_PROGRAMS += ca-certificates
# JPEG (ws074-p019): libjpeg-compat, before the browser uses it (p021).
ZEDBSD_USER_PROGRAMS += libjpeg-compat
