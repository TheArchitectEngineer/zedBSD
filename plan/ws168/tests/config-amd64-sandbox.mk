# ws168-p002: the image of plan/ws168/tests/sandbox-p002.sh: the Files image (plan/tools/files/config-amd64-files.mk)
# with the kernel's sandbox test (sandboxtest, and its static child /usr/libexec/sandbox-child) and runas.  A test image
# only.
#   plan/tools/guest/test-image.sh plan/ws168/tests/config-amd64-sandbox.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += sandboxtest sandbox-child runas
