# ws035-p013〜: the lean zdesktop guest image (plan/tools/files/config-amd64-files.mk) with the
# networkd stand-in that has a Wi-Fi radio (network-probe), for the system bar's network menu.  Build:
#   plan/ws035/tests/build-network-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += network-probe
