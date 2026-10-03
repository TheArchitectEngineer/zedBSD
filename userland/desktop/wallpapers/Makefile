# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib

# The bundled wallpapers Settings offers (ws089-p009), drawn by generate.py; they are not kept in git.
# A config.mk that sets ZEDBSD_KEILAND_WALLPAPERS := y gets them in /usr/share/keiland/wallpapers (ws136-p001,
# 2026-10-04: a test image is a config.mk build plus files of the tree, so the build makes them, not the test's
# script).  The default is n: the release image is unchanged.  The Linux and FreeBSD builds install the same five
# (keiland-linux.mk, keiland-freebsd.mk).
ZEDBSD_KEILAND_WALLPAPERS ?= n
ZEDBSD_KEILAND_WALLPAPER_NAMES := Aurora Dawn Lagoon Meadow Twilight
ifeq ($(ZEDBSD_KEILAND_WALLPAPERS),y)
ZEDBSD_KEILAND_WALLPAPER_FILES := $(addprefix $(BUILD)/wallpapers/,$(addsuffix .ppm,$(ZEDBSD_KEILAND_WALLPAPER_NAMES)))
$(ZEDBSD_KEILAND_WALLPAPER_FILES) &: userland/desktop/wallpapers/generate.py
	$(PYTHON) userland/desktop/wallpapers/generate.py $(BUILD)/wallpapers >/dev/null
ZEDBSD_PACKAGE_INPUTS += $(ZEDBSD_KEILAND_WALLPAPER_FILES)
ZEDBSD_PACKAGE_FILES += $(foreach name,$(ZEDBSD_KEILAND_WALLPAPER_NAMES),\
	--file /usr/share/keiland/wallpapers/$(name).ppm=$(BUILD)/wallpapers/$(name).ppm)
endif
