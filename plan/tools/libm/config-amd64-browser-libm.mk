# ws076 (moved to plan/tools/libm): the lean amd64 image with the serial mirror and browser, so
# plan/tools/libm/browser-js.sh can run the browser's own JavaScript tests
# (plan/ws074/tests/js) in the guest with the new libm.
include plan/tools/libm/config-amd64-libm.mk
ZEDBSD_USER_PROGRAMS += browser
