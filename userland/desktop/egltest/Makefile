include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# EGL and OpenGL ES test (WS068): clears or draws into a Wayland window or the screen and presents it.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
EGLTEST_SOURCES := userland/desktop/egltest/main.c userland/desktop/egltest/scene.c userland/desktop/egltest/cube.c \
	userland/desktop/egltest/es3.c userland/desktop/egltest/formats.c \
	userland/desktop/egltest/volumes.c userland/desktop/egltest/targets.c \
	userland/desktop/egltest/blits.c userland/desktop/egltest/queries.c userland/desktop/egltest/feedback.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,egltest,EGL test,amd64,y,basic,$(EGLTEST_SOURCES),y,desktop,desktop/egltest,desktop/libegl desktop/libglesv2 desktop/libwayland-egl desktop/libwayland,0755,program,,,bin))
