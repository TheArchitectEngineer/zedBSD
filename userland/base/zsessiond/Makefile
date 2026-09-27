# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib

# The graphical login's session manager (plan/ws035/login-manager-design.md): the service "greeter", which
# replaces the console's getty when the boot parameters ask for the graphical login (login=graphical).
include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

ZSESSIOND_SOURCES := userland/base/zsessiond/main.c userland/base/zsessiond/greeter.c \
	userland/base/zsessiond/session.c userland/base/zsessiond/seat.c userland/base/login/verify.c
ZSESSIOND_DATA := /etc/service.d/greeter=userland/base/zsessiond/greeter.service \
	/etc/zdesktop/session=userland/base/zsessiond/session.sh
$(eval $(call ZEDBSD_USERLAND_PACKAGE,zsessiond,graphical login session manager,amd64,y,basic,$(ZSESSIOND_SOURCES),y,base,base/zsessiond,,0755,program,$(ZSESSIOND_DATA),,sbin))
