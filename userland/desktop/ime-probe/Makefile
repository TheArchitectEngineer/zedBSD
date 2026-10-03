include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# A test client of the text input protocol (ws095-p004): a window whose text input logs what the input method
# sends, for plan/ws095/tests/ime-p004.sh.  Not in any image by default.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
$(eval $(call ZEDBSD_USERLAND_PACKAGE,ime-probe,Input method test client,amd64,n,basic,userland/desktop/ime-probe/main.c,y,desktop,desktop/ime-probe,desktop/libwayland,0755,program,,,bin))
