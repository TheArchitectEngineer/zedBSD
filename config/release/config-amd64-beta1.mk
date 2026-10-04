# The beta 1 release image (ws129-p004; plan/ws129/release.md section 4 and
# the user's decisions U2, U4, U6 and U10 there): the CI image's
# configuration with the release's differences.  The release workflow
# (.github/workflows/release.yml) builds it from an rc tag; locally:
#   make ZEDBSD_CONFIG=config/release/config-amd64-beta1.mk BUILD=build/release
# sshd and the password login stay as in the CI image (U4).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk

# A release build: uname's release is VERSION itself, without the source's revision (ws129-p003).
ZEDBSD_RELEASE_BUILD := y

# Nobody logs in as root: its password is locked (U10).  kei is the user.
ZEDBSD_ROOT_LOCKED := y

# The old installer stays out (U2); the development environment (the headers, clang and libc++) and Emacs are in.
ZEDBSD_USER_PROGRAMS := $(filter-out zedinst,$(ZEDBSD_USER_PROGRAMS))
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),clang libcxx emacs)

# FFmpeg's libraries (LGPL 2.1 or later, ws122-p001) and the simple video player are in from beta 1 (the user's decision of 2026-10-05).
ZEDBSD_USER_PROGRAMS += $(filter-out $(ZEDBSD_USER_PROGRAMS),libavcodec)

# The Windows zip (U6): n until the base archive is rebuilt with the fork's confirmed commits (ws088-p002);
# y makes the release job package it, refusing a draft base, and fail when it cannot.
ZEDBSD_RELEASE_ZIP := n
