# ws089 (Settings): the lean zdesktop guest image for the Venus tests, the file manager's image
# (plan/tools/files/config-amd64-files.mk: no clang, lldb or libcxx) with Settings.
# Build:
#   plan/ws089/tests/build-settings-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += settings
