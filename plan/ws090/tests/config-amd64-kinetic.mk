# ws090-p017 (BUG-211, BUG-218): the pen test guest (the injector's test touch pad, touchinject) with Settings, for
# a touch pad's two-finger scrolling that flies on.  A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws090/tests/config-amd64-kinetic.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws079/tests/config-amd64-pen.mk
ZEDBSD_USER_PROGRAMS += settings
