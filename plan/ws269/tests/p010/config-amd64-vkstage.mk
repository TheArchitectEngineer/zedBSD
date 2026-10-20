# ws269-p010: amd64 Venus test image with vkcompute, vkimage and vkstage
# (gpu-selftest's clear, copy, draw, sample, depth and present stages through Venus).
#   make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-amd64-vkstage.mk BUILD=build/<dir> disk-image
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/tools/guest/config-amd64-ssh.mk
ZEDBSD_USER_PROGRAMS += libwayland-client libvulkan vkcompute vkimage vkstage
