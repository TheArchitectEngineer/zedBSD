# ws090-p019: the kinetic test guest (plan/ws090/tests/config-amd64-kinetic.mk: the injector's touch pad and
# Settings) with the programs whose two-finger scrolling now flies on through libkeiland's scroller: the terminal,
# the text editor, the file manager, Phone, Mail and Calendar.  A test image only.  Build:
#   plan/tools/guest/test-image.sh plan/ws090/tests/config-amd64-kinetic-apps.mk BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws090/tests/config-amd64-kinetic.mk
ZEDBSD_USER_PROGRAMS += textedit files phone mailer calendar
