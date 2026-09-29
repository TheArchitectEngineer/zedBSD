# ws095 (IME): the lean zdesktop guest image for the Venus tests (the Files image, plan/tools/files/config-amd64-files.mk:
# no clang, lldb or libcxx) with the input method, its dictionaries and the test client.
# Build:
#   plan/ws095/tests/build-ime-image.sh [BUILD]
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += keiland-ime ime-dict-ja ime-probe
