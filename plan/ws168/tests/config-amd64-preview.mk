# ws168-p003: the build of keiland-preview (the static preview maker) with the sandbox test image's programs
# (plan/ws168/tests/config-amd64-sandbox.mk).  Build the program:
#   make ZEDBSD_CONFIG=plan/ws168/tests/config-amd64-preview.mk BUILD=BUILD BUILD/bin/keiland-preview
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws168/tests/config-amd64-sandbox.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),keiland-preview)
