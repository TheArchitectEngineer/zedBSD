include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Fills and copies through the GHCI driver's native command (root, test kernels only) and checks the memory.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
$(eval $(call ZEDBSD_USERLAND_PACKAGE,ghci-blit,GHCI blitter test,amd64 visionfive2,n,basic,userland/tests/ghci-blit/main.c,y,tests,tests/ghci-blit,,0755,program,,,bin))
