#!/bin/sh
# ws115-p009: the criteria image (plan/ws099/tests/config-amd64-criteria.mk, generated wallpapers included) with the
# guest harness's files, the sample home maker and a probe's files (PROBE-EXTRA: --file and --mode pairs).
# ws136-p001 (2026-10-04): through plan/tools/guest/test-image.sh; the fonts come with the compositor's package.
#
#   plan/ws115/tests/build-desktop-image.sh BUILD PROBE-EXTRA [MAKE-VARIABLE...]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=$1
probe=$2
shift 2
# PROBE-EXTRA is a list of --file and --mode pairs, split into words.
# shellcheck disable=SC2086
exec plan/tools/guest/test-image.sh plan/ws099/tests/config-amd64-criteria.mk "$build" \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh \
	$probe \
	ZEDBSD_TEST_IMAGE_TAG=wsp009 "$@"
