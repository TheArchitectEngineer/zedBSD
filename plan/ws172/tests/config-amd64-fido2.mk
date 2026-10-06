# ws172-p003: the passkey image (config-amd64-passkey.mk: the graphical login with /sbin/passkey, account-admin,
# Settings and su) with the security key style: /usr/libexec/passkey-fido2 and OpenSSL's libcrypto, fidoctl, and the
# test kernel's loopback security key (CONFIG_SECURITY_KEY_TEST_LOOPBACK, /dev/input/hidraw0).  A test image only.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws172/tests/config-amd64-passkey.mk
CONFIG_SECURITY_KEY_TEST_LOOPBACK := y
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),openssl passkey-fido2 fidoctl)
