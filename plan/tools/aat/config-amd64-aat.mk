# ws173-p003: the AAT image (Agent Acceptance Test): the Latitude 5330 UAT image (plan/ws159/tests/config-amd64-uat.mk,
# the release's configuration as a development build) with the test input injector and the screen's capture, so that
# plan/tools/aat/aat drives it over SSH.  A test image only: neither goes into a product's configuration.
# Build (root's SSH key and the host keys of the guest harness, not its net.conf, which would replace the machine's
# network):
#   plan/tools/aat/build-image.sh build/aat
include plan/ws159/tests/config-amd64-uat.mk
# /dev/input-inject (ws173-p001 adds a mouse and a keyboard to it).
CONFIG_INPUT_TEST_INJECT := y
# The injector's daemon and the capture command (ws173-p001, p002, P1): their package names go here when they land,
# for example
#   ZEDBSD_USER_PROGRAMS += aatinject keiland-shot
# with whatever setting the compositor's test-only capture needs.  Until then the image boots and aat check names
# what is missing.
