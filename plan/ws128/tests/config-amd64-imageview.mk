# ws128-p005: the lean Files guest image (plan/tools/files/config-amd64-files.mk) with Image Viewer (libkeiui,
# libgif-compat), for plan/ws128/tests/imageview-p005.sh.  Build:
#   plan/tools/guest/test-image.sh plan/ws128/tests/config-amd64-imageview.mk BUILD
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += libgif-compat libkeiui imageview
