# ws217-p002b/p003: the test image of the compositor's turn pass and of the input's mapping (plan/ws217/phase002b/phase.md
# section 6, plan/ws217/phase003/phase.md section 12): the AAT image (plan/tools/aat/config-amd64-aat.mk: the UAT
# configuration, the input injector, aat-input, peninject and the screen's capture, whose requests POLICY, ROTATE and
# POINTER set a test's angles and read the pointer while the session runs), with wltest for plain windows (p002b's B
# moves one) and tablet-probe for what a client hears of a pen and of wl_touch (p003's cancels), and keiland-system for
# libkeiland's side of a display's angle (p004: display-rotate KEY ANGLE, displays).  A test image only; no file is
# copied.
#   AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh BUILD
include plan/tools/aat/config-amd64-aat.mk
ZEDBSD_USER_PROGRAMS += wltest tablet-probe keiland-system
