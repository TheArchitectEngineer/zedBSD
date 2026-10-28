include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# install-admin: the installer's helper (userland/desktop/installer/admin), set-user-ID root, mode 4550 (owner root,
# group wheel), in /usr/libexec (outside PATH).  The menu installer is not selectable: only make installer-image
# builds it, into the installer's image alone (platform/amd64/installer-image.mk), for it writes disks as root
# without asking for a password.  It reads the units with libz-compat's inflate, linked in.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
INSTALL_ADMIN_SOURCES := userland/desktop/installer/admin/main.c userland/desktop/installer/admin/index.c \
	userland/desktop/installer/admin/unpack.c userland/desktop/installer/admin/plan.c \
	userland/desktop/installer/admin/target.c userland/desktop/installer/admin/probe.c \
	userland/desktop/installer/admin/session.c userland/base/common/account.c userland/base/common/sha256.c \
	userland/base/diskpart/table.c userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,install-admin,install-admin,amd64,n,basic,$(INSTALL_ADMIN_SOURCES),n,installer,,,4550,program,,,usr/libexec))
