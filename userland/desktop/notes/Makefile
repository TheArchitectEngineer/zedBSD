include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Notes, the handwritten notebook (plan/ws079/design-input-notes.md section 5): pages of pen strokes drawn with
# Vulkan in a Wayland window, saved as PDF with libpdf (vector outlines and the edit data), with a journal for recovery.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KEILAND_NOTES_SOURCES := userland/desktop/notes/main.c \
	userland/desktop/notes/document.c userland/desktop/notes/encode.c \
	userland/desktop/notes/journal.c userland/desktop/notes/save.c \
	userland/desktop/notes/geometry.c userland/desktop/notes/render.c \
	userland/desktop/notes/window.c userland/desktop/notes/menu.c \
	userland/desktop/notes/ui.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,notes,Handwritten notebook,amd64,y,basic,$(KEILAND_NOTES_SOURCES),y,desktop,desktop/notes,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype base/libpdf,0755,program,,,bin))
