# ws128-p005: the lean Files guest image (plan/tools/files/config-amd64-files.mk) with Image Viewer (libkeiland,
# libgif-compat), for plan/ws128/tests/imageview-p005.sh.  Build:
#   plan/tools/guest/test-image.sh plan/ws128/tests/config-amd64-imageview.mk BUILD
# Files' guest tests need the sample home (/usr/share/files-tests/make-home.sh), which only plan/tools/files/build-files-image.sh adds:
#   FILES_CONFIG=plan/ws128/tests/config-amd64-imageview.mk sh plan/tools/files/build-files-image.sh BUILD (T1-081, 2026-10-04).
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += libgif-compat libkeiland imageview
