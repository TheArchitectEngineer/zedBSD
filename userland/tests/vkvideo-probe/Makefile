include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The Vulkan Video probe (ws083): lists a device's video decode, decodes an H.264 stream and prints each frame's SHA-256.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
$(eval $(call ZEDBSD_USERLAND_PACKAGE,vkvideo-probe,Vulkan Video probe,amd64,n,basic,userland/tests/vkvideo-probe/main.c userland/tests/vkvideo-probe/h264.c userland/tests/vkvideo-probe/dpb.c userland/tests/vkvideo-probe/frame.c userland/base/common/sha256.c,y,tests,tests/vkvideo-probe,desktop/libvulkan,0755,program,,,bin))
