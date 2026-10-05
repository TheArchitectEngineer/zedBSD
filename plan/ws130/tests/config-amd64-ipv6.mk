# ws130-p002: the image of plan/ws130/tests/ipv6-p002.sh: the Files image (plan/tools/files/config-amd64-files.mk) with the
# IPv6 kernel probe and runas.  A test image only.
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/files/config-amd64-files.mk
ZEDBSD_USER_PROGRAMS += ipv6-probe runas
