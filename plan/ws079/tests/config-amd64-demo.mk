# ws079-p016: the guest image for the demonstration's S8 and S9 (demo-s8-s9.sh): the Notes image
# (config-amd64-notes.mk: the Files image with PDF Viewer, libpdf, Notes, the test injector and peninject) with
# touchinject.  A test image only (the injector is never in a default or release image).
include plan/ws079/tests/config-amd64-notes.mk
ZEDBSD_USER_PROGRAMS += touchinject
