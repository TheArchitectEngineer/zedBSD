include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# A TCP throughput probe: connects to a host's peer (plan/ws221/tests/perf-peer.py) and sends or receives for a number
# of seconds, printing the rate each second and at the end.  The same source builds on Linux, so a Linux machine is
# measured with the same program.  Test images only.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
$(eval $(call ZEDBSD_USERLAND_PACKAGE,tcp-perf,TCP throughput probe,*,n,basic,userland/tests/tcp-perf/main.c,y,tests,tests/tcp-perf,,0755,program,,,bin))
