include $(dir $(lastword $(MAKEFILE_LIST)))../../base/package.mk

# OpenGL with GLX for Xzed (WS069 p004, p005): the OpenGL ES translation to Vulkan (libGLESv2's sources),
# the fixed-function OpenGL 1.x over it, OpenGL 3.0's calls that OpenGL ES 3.0 does not have (WS068 p013), and GLX, which draws into an EGL pbuffer and puts each frame into
# the X window; libX11 is built in (X programs build it into themselves too).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# libGLESv2's GLSL compiler (WS068 p015-p019), built in with the rest of libGLESv2's sources.
LIBGL_GLSL_SOURCES := userland/desktop/libglesv2/glsl/arena.c userland/desktop/libglesv2/glsl/lex.c \
	userland/desktop/libglesv2/glsl/preprocess.c userland/desktop/libglesv2/glsl/parse.c userland/desktop/libglesv2/glsl/types.c \
	userland/desktop/libglesv2/glsl/builtins.c userland/desktop/libglesv2/glsl/check.c userland/desktop/libglesv2/glsl/fold.c \
	userland/desktop/libglesv2/glsl/glsl.c userland/desktop/libglesv2/glsl/module.c userland/desktop/libglesv2/glsl/emit.c \
	userland/desktop/libglesv2/glsl/emit-builtin.c userland/desktop/libglesv2/glsl/link.c
LIBGL_SOURCES := userland/retro/libGL/glx.c userland/retro/libGL/fixed.c userland/retro/libGL/immediate.c userland/retro/libGL/gl3.c \
	userland/retro/libX11/xlib.c \
	userland/desktop/libglesv2/gles.c userland/desktop/libglesv2/buffer.c userland/desktop/libglesv2/texture.c \
	userland/desktop/libglesv2/spirv.c userland/desktop/libglesv2/program.c userland/desktop/libglesv2/draw.c \
	userland/desktop/libglesv2/framebuffer.c userland/desktop/libglesv2/format.c userland/desktop/libglesv2/pixels.c userland/desktop/libglesv2/query.c userland/desktop/libglesv2/feedback.c $(LIBGL_GLSL_SOURCES)

# The public SONAME follows the standard spelling.
LIBGL_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libGL.so=$(BUILD)/dynamic/libGL.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libgl,OpenGL and GLX,amd64,y,library,$(LIBGL_SOURCES),y,x11-applications,X11/libGL,desktop/libegl,,shared-library,$(LIBGL_IMAGE_DATA),,lib))
