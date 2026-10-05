include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Phone (WS170 p000): the mock of the application that puts a person's messages and calls in one timeline;
# the contacts and their timelines are test data in the program, and there is no backend yet.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
PHONE_SOURCES := userland/desktop/phone/main.c userland/desktop/phone/view.c userland/desktop/phone/data.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,phone,Phone,amd64,n,basic,$(PHONE_SOURCES),y,desktop,desktop/phone,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype,0755,program,,,bin))
