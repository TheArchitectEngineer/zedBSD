# ws173-p003: the AAT image (Agent Acceptance Test): the Latitude 5330 UAT image (plan/ws159/tests/config-amd64-uat.mk,
# the release's configuration as a development build) with the test input injector and the screen's capture, so that
# plan/tools/aat/aat drives it over SSH.  A test image only: neither goes into a product's configuration.
# Build (root's SSH key and the host keys of the guest harness, not its net.conf, which would replace the machine's
# network):
#   plan/tools/aat/build-image.sh build/aat
include plan/ws159/tests/config-amd64-uat.mk
# /dev/input-inject, and aat-input (ws173-p001, P1): its server holds a mouse, an absolute pointer and a keyboard.
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += aat-input
# The compositor's test-only capture, keiland-shot (ws173-p002, P1): its package and setting go here when it lands.
# Until then aat check names it as missing and aat shot fails.
