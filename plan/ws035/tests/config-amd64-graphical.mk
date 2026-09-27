# ws035-p098: the login image (config-amd64-login.mk) with the default graphical boot: zedbsd.cfg gets
# logo=logo.ppm, kmsg=quiet and login=graphical, so the machine boots to the greeter.  Build (the same build
# directory as the login image; copy the result before building the other one):
#   plan/ws035/tests/build-login-image.sh [BUILD] graphical
include plan/ws035/tests/config-amd64-login.mk
ZEDBSD_GRAPHICAL_BOOT := y
