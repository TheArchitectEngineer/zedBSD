# Isolated context containment regression with a shorter execution deadline.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include $(dir $(lastword $(MAKEFILE_LIST)))config-wayland-amd64.mk
CONFIG_GPU_JOB_EXECUTION_MS := 8000
CONFIG_GPU_JOB_STOP_MS := 10000
