# zedbsd-nightly.zip: the Windows package (WINQ-EMU QEMU with Venus) plus the
# amd64 disk image (ws088-p003).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The image-less base archive is published once, like the LLVM cache, on the
# rev-0 release and pinned here by name and SHA-256. It is made by
# tools/release/make-zedbsd-nightly-base.py. `make zedbsd-nightly-zip` downloads it
# into build/releases/ (or reuses a verified copy there), then writes
# $(BUILD)/zedbsd-nightly.zip with $(ZEDBSD_NIGHTLY_IMAGE) as
# Kei-nightly/data/hdd-image.img. It packages the image already built by
# `make` and does not rebuild it (CI runs it right after the build).

override ZEDBSD_NIGHTLY_BASE_TAG := rev-0
# The asset keeps its first name, and the archive its top directory Kei-nightly/, until a base archive under the
# zedBSD name is published (the upload is the user's decision): changing the name here before the upload breaks
# every download.
override ZEDBSD_NIGHTLY_BASE_ASSET := kei-nightly-base-winq-a10-1.zip
# Draft value (ws088-p001, fork commits unconfirmed). Replace it with the
# digest of the published asset before the upload (ws088-p002).
override ZEDBSD_NIGHTLY_BASE_SHA256 := 81120981aabc80de6dbdc526b7dfa314ee84a8041a1c3b137081e078a81f665c

ZEDBSD_NIGHTLY_FETCH ?= curl --fail --location --silent --show-error
ZEDBSD_NIGHTLY_BASE_URL := https://github.com/awemorris/zedBSD/releases/download/$(ZEDBSD_NIGHTLY_BASE_TAG)/$(ZEDBSD_NIGHTLY_BASE_ASSET)
ZEDBSD_NIGHTLY_BASE_ARCHIVE := $(abspath build/releases/$(ZEDBSD_NIGHTLY_BASE_ASSET))
ZEDBSD_NIGHTLY_IMAGE ?= $(BUILD)/hdd-image.img
ZEDBSD_NIGHTLY_ZIP ?= $(BUILD)/zedbsd-nightly.zip
# 1 accepts a draft base archive.  The draft (fork commits unconfirmed) is on
# the rev-0 release by the user's decision (2026-09-29, ws088-p002); set this
# back to empty once the base is rebuilt with the fork commits.
ZEDBSD_NIGHTLY_ALLOW_DRAFT ?= 1

.PHONY: zedbsd-nightly-base zedbsd-nightly-zip
zedbsd-nightly-base:
	@set -e; archive='$(ZEDBSD_NIGHTLY_BASE_ARCHIVE)'; download=; \
	trap 'test -z "$$download" || rm -f -- "$$download"' EXIT; \
	if test -f "$$archive"; then \
		actual=$$(sha256sum "$$archive" | awk '{print $$1}'); \
		test "$$actual" = '$(ZEDBSD_NIGHTLY_BASE_SHA256)' || \
			{ echo "zedbsd-nightly: cached $$archive does not match the pinned SHA-256" >&2; exit 1; }; \
	else \
		mkdir -p "$$(dirname "$$archive")"; \
		download=$$(mktemp "$$archive.XXXXXX"); \
		$(ZEDBSD_NIGHTLY_FETCH) --output "$$download" '$(ZEDBSD_NIGHTLY_BASE_URL)'; \
		actual=$$(sha256sum "$$download" | awk '{print $$1}'); \
		test "$$actual" = '$(ZEDBSD_NIGHTLY_BASE_SHA256)' || \
			{ echo 'zedbsd-nightly: downloaded base archive SHA-256 mismatch' >&2; exit 1; }; \
		mv -- "$$download" "$$archive"; download=; \
	fi

# The image is the amd64 one; other platforms are refused before anything
# (such as their own disk image) is built.
ifeq ($(ZEDBSD_PLATFORM_DIR),amd64)
zedbsd-nightly-zip: zedbsd-nightly-base
	@test -f '$(ZEDBSD_NIGHTLY_IMAGE)' || \
		{ echo 'zedbsd-nightly: missing $(ZEDBSD_NIGHTLY_IMAGE); run make first' >&2; exit 1; }
	$(PYTHON) tools/release/make-zedbsd-nightly-zip.py \
		--base '$(ZEDBSD_NIGHTLY_BASE_ARCHIVE)' --sha256 '$(ZEDBSD_NIGHTLY_BASE_SHA256)' \
		--image '$(ZEDBSD_NIGHTLY_IMAGE)' --output '$(ZEDBSD_NIGHTLY_ZIP)' \
		$(if $(filter 1,$(ZEDBSD_NIGHTLY_ALLOW_DRAFT)),--allow-draft)
else
zedbsd-nightly-zip:
	@echo 'zedbsd-nightly: zedbsd-nightly.zip carries the amd64 image; use an amd64 config' >&2; exit 1
endif
