# ws197-p007a2 L1-L3: the UAT image (config/current-uat.mk) with bt-sco-loopback, which streams SCO through the
# controller's HCI Local Loopback (userland/tests/bt-sco-loopback; bluetoothd stopped while it runs), and bluetoothd's
# "BLUETOOTHD CODECS" line at its start (L1).  For the Latitude 5330 (Intel AX211).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/current-uat.mk
ZEDBSD_USER_PROGRAMS += bt-sco-loopback
