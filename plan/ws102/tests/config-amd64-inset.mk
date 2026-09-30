# ws102-p015: the WS079 demonstration image (Notes, PDF Viewer, the injected touch screen and pen, touchinject) with
# Text Editor, for the keyboard inset's check (inset-guest.sh).  A test image only.
# Build:
#   plan/ws102/tests/build-inset-image.sh BUILD
include plan/ws079/tests/config-amd64-demo.mk
ZEDBSD_USER_PROGRAMS += textedit
