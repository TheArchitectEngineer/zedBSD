# ws079-p005 (Notes): the lean zdesktop guest image of the File Manager tests
# (plan/tools/files/config-amd64-files.mk: the Venus driver, the compositor, the terminal,
# files, no clang or lldb) with libpdf and Notes.  Build:
#   plan/tools/files/build-files-image.sh build/amd64   (with ZEDBSD_CONFIG pointing here; see run-notes-guest.sh)
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += libpdf notes
