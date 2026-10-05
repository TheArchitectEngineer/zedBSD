# ws172-p002: the graphical login image (plan/ws035/tests/config-amd64-graphical.mk) with /sbin/passkey and
# account-admin, for plan/ws172/tests/passkey-p002-guest.sh.  Built with plan/ws172/tests/build-passkey-image.sh.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws035/tests/config-amd64-graphical.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),passkey account-admin settings)
