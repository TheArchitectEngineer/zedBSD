# ws134-p002: the lean Files image (plan/tools/files/config-amd64-files.mk: zdesktop, the fonts, the guest harness) with
# the System Monitor.  Build with plan/ws134/tests/build-monitor-image.sh BUILD.
# ws134-p004: with the test touch screen (/dev/input-inject, CONFIG_INPUT_TEST_INJECT=y, and touchinject, as
# plan/ws089/tests/config-amd64-settings-touch.mk has them) for the taps, the swipe and the pinches of monitor-p004.sh.
# A test image only.
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += monitor
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += touchinject
