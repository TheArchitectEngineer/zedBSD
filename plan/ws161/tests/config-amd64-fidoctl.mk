# ws161-p004: the raw HID image (config-amd64-hidraw.mk: the Files image with the test kernel's loopback key) with the
# security key tool fidoctl (userland/base/fidoctl: libpasskey and OpenSSL's libcrypto).
include plan/ws161/tests/config-amd64-hidraw.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),openssl fidoctl)
