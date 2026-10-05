include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# account-admin (ws089-p026; docs/architecture/security.md, "Account administration"): the administrator's changes to
# the people's accounts for Settings, set-user-ID root, in /usr/libexec (outside PATH).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ACCOUNT_ADMIN_SOURCES := userland/base/account-admin/main.c userland/base/account-admin/edit.c \
	userland/base/common/account.c userland/base/login/verify.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,account-admin,account-admin,*,y,basic,$(ACCOUNT_ADMIN_SOURCES),y,base,,,4555,program,,,usr/libexec))
