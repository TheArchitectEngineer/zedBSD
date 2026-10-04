# UAT 2026-10-04 (plan/uat.md): the demo image (plan/ws075/demo/config-demo-hdmi.mk, the CI amd64 configuration
# plus Settings, zterm, gpudemo and the wallpapers) with System Monitor (WS134) and the colour emoji font.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws075/demo/config-demo-hdmi.mk
ZEDBSD_USER_PROGRAMS += monitor
ZEDBSD_USER_PROGRAMS += noto-color-emoji
