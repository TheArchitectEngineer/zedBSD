include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Text Editor (WS092, plan/ws092/design.md): plain UTF-8 text in a Wayland window, drawn on the CPU and
# shown with Vulkan; undo and redo, the clipboard and the primary selection, finding in the titlebar's field,
# and the touch screen's inertial scrolling.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
TEXTEDIT_SOURCES := userland/desktop/textedit/main.c userland/desktop/textedit/window.c \
	userland/desktop/textedit/present.c userland/desktop/textedit/menu.c \
	userland/desktop/textedit/titlebar.c userland/desktop/textedit/glass.c \
	userland/desktop/textedit/clipboard.c userland/desktop/textedit/primary.c \
	userland/desktop/textedit/touch.c userland/desktop/textedit/app.c \
	userland/desktop/textedit/edit.c userland/desktop/textedit/draw.c \
	userland/desktop/textedit/buffer.c userland/desktop/textedit/undo.c \
	userland/desktop/textedit/file.c userland/desktop/textedit/layout.c \
	userland/desktop/textedit/find.c userland/desktop/textedit/keys.c \
	userland/desktop/textedit/canvas.c userland/desktop/textedit/text.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,textedit,Text Editor,amd64,y,basic,$(TEXTEDIT_SOURCES),y,desktop,desktop/textedit,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype,0755,program,,,bin))
