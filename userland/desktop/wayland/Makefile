include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Wayland compositor: window mode (Vulkan) and fullscreen mode (direct scanout).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KEILAND_SOURCES := userland/desktop/wayland/main.c userland/desktop/wayland/wire.c \
	userland/desktop/wayland/objects.c userland/desktop/wayland/protocol.c userland/desktop/wayland/display.c \
	userland/desktop/wayland/seat.c userland/desktop/wayland/input.c userland/desktop/wayland/tablet.c \
	userland/desktop/wayland/compose.c userland/desktop/wayland/import.c userland/desktop/wayland/shm.c \
	userland/desktop/wayland/glass.c userland/desktop/wayland/shell.c userland/desktop/wayland/home.c \
	userland/desktop/wayland/menu.c userland/desktop/wayland/menu-shell.c userland/desktop/wayland/titlebar.c \
	userland/desktop/wayland/popup.c userland/desktop/wayland/toplevel.c userland/desktop/wayland/subsurface.c userland/desktop/wayland/keymap.c userland/desktop/wayland/data.c \
	userland/desktop/wayland/icons.c userland/desktop/wayland/titlebar-shell.c \
	userland/desktop/wayland/decoration.c userland/desktop/wayland/cursor.c userland/desktop/wayland/viewport.c \
	userland/desktop/wayland/panels.c userland/desktop/wayland/backdrop.c userland/desktop/wayland/damage.c userland/desktop/wayland/greeter.c userland/desktop/wayland/primary.c \
	userland/desktop/wayland/handoff.c userland/desktop/wayland/network.c \
	userland/desktop/vkdemo/display.c userland/desktop/artwork/mark.c
# App Home starts X11 applications through zdesktop-x11, which starts xserver when it is needed.
ZEDBSD_PACKAGE_INPUTS += userland/desktop/wayland/keiland-x11
ZEDBSD_PACKAGE_FILES += $(if $(filter wayland,$(ZEDBSD_USER_PROGRAMS)),--file /usr/libexec/keiland-x11=userland/desktop/wayland/keiland-x11)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,wayland,Wayland compositor,amd64,y,basic,$(KEILAND_SOURCES),y,desktop,desktop/wayland,desktop/libvulkan desktop/libtruetype desktop/libkeiland desktop/sessiond,0755,program,,,bin))
