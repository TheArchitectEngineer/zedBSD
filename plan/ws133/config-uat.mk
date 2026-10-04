# UAT 2026-10-04 (plan/uat.md): the demo image (plan/ws075/demo/config-demo-hdmi.mk, the CI amd64 configuration
# plus Settings, zterm, gpudemo and the wallpapers) with System Monitor (WS134), Emacs (ws129-p012) and the colour
# emoji font, each added only when the CI configuration does not list it already.  App Home: plan/ws133/uat-apps.conf.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include plan/ws075/demo/config-demo-hdmi.mk
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),monitor emacs noto-color-emoji)
