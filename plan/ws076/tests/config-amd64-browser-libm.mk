# ws076: the lean amd64 image with the serial mirror and zdesktop-browser, so
# plan/ws076/tests/browser-js.sh can run the browser's own JavaScript tests
# (plan/ws074/tests/js) in the guest with the new libm.
include plan/ws076/tests/config-amd64-libm.mk
ZEDBSD_USER_PROGRAMS += zdesktop-browser
