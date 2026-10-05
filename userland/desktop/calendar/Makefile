include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Calendar (WS155 p000): the mock of Keiland's calendar -- the months with the events of four calendars (test data in
# the program), a panel to add an event by dragging its kind onto a date, and a desk calendar drawn in 3D by a small
# software renderer; nothing is stored.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
CALENDAR_SOURCES := userland/desktop/calendar/main.c userland/desktop/calendar/view.c \
	userland/desktop/calendar/data.c userland/desktop/calendar/date.c \
	userland/desktop/calendar/scene.c userland/desktop/calendar/render3d.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,calendar,Calendar,amd64,n,basic,$(CALENDAR_SOURCES),y,desktop,desktop/calendar,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype,0755,program,,,bin))
