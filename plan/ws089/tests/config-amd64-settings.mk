# ws089 (Settings): the lean zdesktop guest image for the Venus tests, the file manager's image
# (plan/tools/files/config-amd64-files.mk: no clang, lldb or libcxx) with Settings.
# Build:
#   plan/ws089/tests/build-settings-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += settings
# ws089-p003: the networkd stand-in with a Wi-Fi radio (QEMU has none), for the Wi-Fi page.
ZEDBSD_USER_PROGRAMS += network-probe
