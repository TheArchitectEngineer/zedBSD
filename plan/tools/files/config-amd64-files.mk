# ws071 (File Manager): a lean zdesktop guest image for the Venus tests, the ws070
# System Menu image (plan/tools/titlebar/config-amd64-menu.mk) with files.
# Build:
#   plan/tools/files/build-files-image.sh [BUILD]
include plan/tools/titlebar/config-amd64-menu.mk
ZEDBSD_USER_PROGRAMS += libz-compat libpng-compat files
# ws079-p006: PDF Viewer (Files opens PDFs with it) and libpdf (with libjpeg-compat).
ZEDBSD_USER_PROGRAMS += libjpeg-compat libpdf pdfviewer
