# ws090-p013: the guest image for the text input of libkeiland's window: WS095's input method image
# (plan/ws095/tests/config-amd64-ime.mk: the lean zdesktop image with keiland-ime, its dictionaries and ime-probe)
# with Text Editor and libkeiland.
#   make ZEDBSD_CONFIG=plan/ws090/tests/config-amd64-textinput.mk BUILD=build/amd64 ZEDBSD_TEST_EXTRA_FILES=... disk-image
#   (plan/ws090/tests/textinput-p013.sh builds it)
include plan/ws095/tests/config-amd64-ime.mk
ZEDBSD_USER_PROGRAMS += libkeiland textedit
