# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib

# The graphical login's session manager (plan/ws035/login-manager-design.md): the service "greeter", which
# replaces the console's getty when the boot parameters ask for the graphical login (login=graphical).
include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

SESSIOND_SOURCES := userland/desktop/sessiond/main.c userland/desktop/sessiond/greeter.c \
	userland/desktop/sessiond/session.c userland/desktop/sessiond/seat.c userland/base/login/verify.c \
	userland/desktop/sessiond/service.c userland/desktop/sessiond/service-rules.c userland/base/common/account.c \
	userland/base/service/rcconf.c userland/base/service/service-config.c userland/base/service/zsv1-client.c \
	userland/base/service/zsv1-protocol.c
SESSIOND_DATA := /etc/service.d/greeter=userland/desktop/sessiond/greeter.service \
	/etc/keiland/session=userland/desktop/sessiond/session.sh \
	/etc/keiland/autologin=userland/desktop/sessiond/autologin
$(eval $(call ZEDBSD_USERLAND_PACKAGE,sessiond,graphical login session manager,amd64,y,basic,$(SESSIOND_SOURCES),y,desktop,desktop/sessiond,,0755,program,$(SESSIOND_DATA),,sbin))
