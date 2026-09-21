# How much of the build machine a nested build tool may use.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# make itself is told how many jobs to run by the caller's -j, but a build
# driven by cmake decides its own parallelism, so each of those is told this
# instead.  Keeping the answer in one place stops the number from being
# written differently in each of them.
#
# It follows the machine unless the caller says otherwise:
#
#   make ZEDBSD_BUILD_JOBS=8 toolchain
#
# Linking is separated because a link of a large C++ program needs far more
# memory than a compile, and a machine with many cores does not necessarily
# have memory for that many links at once.

ifndef ZEDBSD_BUILD_JOBS_MK
ZEDBSD_BUILD_JOBS_MK := 1

ZEDBSD_BUILD_JOBS ?= $(shell nproc 2>/dev/null || echo 4)
ZEDBSD_BUILD_LINK_JOBS ?= $(shell \
	jobs=$(ZEDBSD_BUILD_JOBS); \
	links=$$((jobs / 8)); \
	if test "$$links" -lt 1; then links=1; fi; \
	echo "$$links")

endif
