include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Mail (WS169 p000): the mock of Keiland's mail application -- accounts, folders, the list, the message and a new
# one to write; the messages are test data in the program, and there is no backend yet.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MAILER_SOURCES := userland/desktop/mailer/main.c userland/desktop/mailer/view.c userland/desktop/mailer/data.c \
	userland/desktop/mailer/tls.c userland/desktop/mailer/conn.c userland/desktop/mailer/imap.c userland/desktop/mailer/smtp.c \
	userland/desktop/mailer/mime.c userland/desktop/mailer/compose.c userland/desktop/mailer/code.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,mailer,Mail,amd64,n,basic,$(MAILER_SOURCES),y,desktop,desktop/mailer,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype,0755,program,,,bin))
