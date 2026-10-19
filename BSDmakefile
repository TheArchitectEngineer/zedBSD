#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# Native *BSD entry for the base system's BSD make, which reads
# BSDmakefile before Makefile. GNU make selects GNUmakefile and keeps
# the full project build interface.
#

GMAKE?= gmake
NATIVE_MAKE= env -u MAKEFLAGS -u MFLAGS ${GMAKE} -j ${.MAKE.JOBS:U1} -f userland/desktop/keiland-freebsd.mk
NATIVE_OVERRIDES= ${.MAKEOVERRIDES:@name@${name}=${${name}:Q}@}

#
# - KEILAND_ASK=n asks nothing and only prints.
# - The packages first, then the build, then the install offered
# - The targets run in the foreground of the terminal
#   (-B: one command at a time, make's own process group):
# - make -j runs each job in a process group of its own,
#   where the questions' read of /dev/tty fails with EIO (T1-503).
# - The build's own parallel jobs are gmake's (-j ${.MAKE.JOBS}).
#
.MAKEFLAGS: -B
KEILAND_ASK?= y
PREREQUISITES= KEILAND_ASK=${KEILAND_ASK} sh tools/build/keiland-prerequisites.sh

keiland-freebsd:
	@${PREREQUISITES} check freebsd
	${NATIVE_MAKE} all ${NATIVE_OVERRIDES}
	@${PREREQUISITES} offer-install freebsd ${NATIVE_MAKE} install ${NATIVE_OVERRIDES}

keiland-freebsd-install:
	${NATIVE_MAKE} install ${NATIVE_OVERRIDES}

help:
	@printf '%s\n' 'make keiland-freebsd: build with the native FreeBSD compiler' \
	    'sudo make keiland-freebsd-install: install under /opt/keiland' \
	    'Other project targets require GNU make (gmake).'

.PHONY: keiland-freebsd keiland-freebsd-install help
