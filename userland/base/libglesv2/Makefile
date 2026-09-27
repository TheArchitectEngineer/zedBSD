include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# OpenGL ES over EGL and Vulkan (WS068).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# The GLSL compiler (WS068 p015-p019), which libGL builds in too.
LIBGLESV2_GLSL_SOURCES := userland/base/libglesv2/glsl/arena.c userland/base/libglesv2/glsl/lex.c \
	userland/base/libglesv2/glsl/preprocess.c userland/base/libglesv2/glsl/parse.c userland/base/libglesv2/glsl/types.c \
	userland/base/libglesv2/glsl/builtins.c userland/base/libglesv2/glsl/check.c userland/base/libglesv2/glsl/fold.c \
	userland/base/libglesv2/glsl/glsl.c userland/base/libglesv2/glsl/module.c userland/base/libglesv2/glsl/emit.c \
	userland/base/libglesv2/glsl/emit-builtin.c userland/base/libglesv2/glsl/link.c
LIBGLESV2_SOURCES := userland/base/libglesv2/gles.c userland/base/libglesv2/buffer.c userland/base/libglesv2/texture.c \
	userland/base/libglesv2/spirv.c userland/base/libglesv2/program.c userland/base/libglesv2/draw.c \
	userland/base/libglesv2/framebuffer.c userland/base/libglesv2/format.c userland/base/libglesv2/pixels.c $(LIBGLESV2_GLSL_SOURCES)

# The public SONAME follows the standard spelling.
LIBGLESV2_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libGLESv2.so=$(BUILD)/dynamic/libGLESv2.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libglesv2,OpenGL ES,amd64,y,library,$(LIBGLESV2_SOURCES),y,base,base/libglesv2,base/libegl,,shared-library,$(LIBGLESV2_IMAGE_DATA),,lib))
