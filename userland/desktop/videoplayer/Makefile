include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Video Player (WS122 p002): opens, plays, pauses, stops and seeks a video file with FFmpeg's libraries (the
# libavcodec package, LGPL), in a libkeiland window; the sound goes to audiod.  amd64 zedBSD (the package's platform).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
VIDEOPLAYER_SOURCES := userland/desktop/videoplayer/main.c userland/desktop/videoplayer/media.c \
	userland/desktop/videoplayer/audio.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,videoplayer,Video Player,amd64,n,basic,$(VIDEOPLAYER_SOURCES),y,desktop,desktop/videoplayer,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype multimedia/libavcodec,0755,program,,,bin))
