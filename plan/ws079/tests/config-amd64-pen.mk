# ws079 p002/p003: the lean Venus guest image (plan/tools/titlebar/config-amd64-menu.mk)
# with the test pen and touch screen: a kernel with /dev/input-inject (CONFIG_INPUT_TEST_INJECT=y),
# peninject, touchinject (ws079-p012) and the tablet probe.  A test image only; the injector is never in a
# default or release image.  Build:
#   plan/ws079/tests/build-pen-image.sh [BUILD]
include plan/tools/titlebar/config-amd64-menu.mk
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += peninject touchinject tablet-probe
