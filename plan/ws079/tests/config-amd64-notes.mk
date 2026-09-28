# ws079-p005 (Notes): the lean zdesktop guest image of the File Manager tests
# (plan/tools/files/config-amd64-files.mk: the Venus driver, the compositor, the terminal,
# files, no clang or lldb) with libpdf and Notes, and the test pen of ws079-p002/p003
# (/dev/input-inject and peninject, as plan/ws079/tests/config-amd64-pen.mk; a test image only).  Build:
#   plan/ws079/tests/build-notes-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
CONFIG_INPUT_TEST_INJECT := y
ZEDBSD_USER_PROGRAMS += libpdf notes peninject tablet-probe
