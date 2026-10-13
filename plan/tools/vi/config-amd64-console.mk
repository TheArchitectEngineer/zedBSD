# ws228-p004: the CI amd64 image with a text boot and the login on the VT
# console, so the console's cursor position and rendition can be seen.
include config/ci/config-amd64.mk
ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_GRAPHICAL_LOGIN := n
