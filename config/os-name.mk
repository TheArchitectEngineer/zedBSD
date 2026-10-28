# The operating system's name for the build (/etc/os-release), read from include/kern/os-name.h so that the name
# is defined once: ZEDBSD_OS_NAME (os-release's NAME) and ZEDBSD_OS_ID (its ID).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZEDBSD_OS_NAME := $(shell sed -n 's/^\#define ZEDBSD_OS_NAME "\(.*\)"$$/\1/p' include/kern/os-name.h)
ZEDBSD_OS_ID := $(shell sed -n 's/^\#define ZEDBSD_OS_ID "\(.*\)"$$/\1/p' include/kern/os-name.h)
ifeq ($(strip $(ZEDBSD_OS_NAME)),)
$(error config/os-name.mk: no ZEDBSD_OS_NAME in include/kern/os-name.h)
endif
ifeq ($(strip $(ZEDBSD_OS_ID)),)
$(error config/os-name.mk: no ZEDBSD_OS_ID in include/kern/os-name.h)
endif
