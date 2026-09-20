# zedBSD userland package: OpenSSH portable (external release tarball).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# OpenSSH is BSD/ISC licensed and stays outside the source tree: only this
# Makefile, the patches beside it and the recorded archive identity live here.
# Provenance and the license audit are in plan/ws032/provenance.md.
#
# Registration as a selectable package and the cross build land with the build
# work; this file currently owns acquisition of the verified source tree.
ZEDBSD_OPENSSH_MAKEFILE := $(lastword $(MAKEFILE_LIST))
ZEDBSD_OPENSSH_DIR := $(patsubst %/,%,$(dir $(ZEDBSD_OPENSSH_MAKEFILE)))
include $(ZEDBSD_OPENSSH_DIR)/../../../base/package.mk
include $(ZEDBSD_OPENSSH_DIR)/../../external.mk

# The latest portable release when this was recorded. Upstream publishes a PGP
# signature rather than a digest file; the recorded digest is what three
# independent mirrors served. The signature is kept beside the archive for a
# separate out-of-band check.
ZEDBSD_EXT_openssh_VERSION := 10.5p1
ZEDBSD_EXT_openssh_ROOT := openssh-10.5p1
ZEDBSD_EXT_openssh_ARCHIVE := openssh-10.5p1.tar.gz
ZEDBSD_EXT_openssh_URL := https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-10.5p1.tar.gz
ZEDBSD_EXT_openssh_SIZE := 2333659
ZEDBSD_EXT_openssh_SHA256 := d44d28a839ea9daf969cc69150fde59910b2b39361dad81a3bd6cbd19218db11
ZEDBSD_EXT_openssh_PATCH_LEVEL := zedbsd0
ZEDBSD_EXT_openssh_PATCHES := $(sort $(wildcard $(abspath $(ZEDBSD_OPENSSH_DIR))/patches/*.patch))

$(eval $(call ZEDBSD_EXTERNAL_SOURCE,openssh))
