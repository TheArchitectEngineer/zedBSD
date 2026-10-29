# ws235: config-amd64-rtl8127.mk with tcp-perf (userland/tests/tcp-perf), for the TCP measurement of
# plan/tools/rtl8127/rtl-link.sh (PERFTEST=1 copies the program to the NVMe's zedBSD for the run).
# Build:  make ZEDBSD_CONFIG=plan/tools/rtl8127/config-amd64-rtl8127-perf.mk BUILD=build/ws235/amd64-c build/ws235/amd64-c/bin/tcp-perf
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/rtl8127/config-amd64-rtl8127.mk

ZEDBSD_USER_PROGRAMS += tcp-perf
