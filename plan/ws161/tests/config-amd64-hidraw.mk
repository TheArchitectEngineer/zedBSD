# ws161-p002: the image of the raw HID checks: the Files image (plan/tools/files/config-amd64-files.mk) with the test
# kernel's loopback security key (CONFIG_SECURITY_KEY_TEST_LOOPBACK, /dev/input/hidraw0) and the probe (hidraw-probe).
# A test image only.  QEMU (T1): plan/ws161/tests/hidraw-p002.sh.
#   FILES_CONFIG=plan/ws161/tests/config-amd64-hidraw.mk plan/tools/files/build-files-image.sh BUILD
include plan/tools/files/config-amd64-files.mk
CONFIG_SECURITY_KEY_TEST_LOOPBACK := y
ZEDBSD_USER_PROGRAMS += hidraw-probe runas
