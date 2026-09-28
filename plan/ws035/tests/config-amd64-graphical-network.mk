# ws035-p104: the graphical login image (config-amd64-graphical.mk) with the networkd stand-in that has a
# Wi-Fi radio (network-probe), for the system bar's network menu in a normal user's session.  Build:
#   plan/ws035/tests/build-login-image.sh [BUILD] graphical-network
include plan/ws035/tests/config-amd64-graphical.mk
ZEDBSD_USER_PROGRAMS += network-probe
