#!/bin/sh
# Builds a test image the one standard way (ws136-p001, 2026-10-04 user: "試験ビルドの標準的な方法は、config.mkでの
# ビルド＋個別ファイルコピー、程度にして、過去のbuild/を参照するのはやめましょう。"): make with the test's config.mk
# and BUILD, plus individual files copied into the image.  The files' sources are in the tree (the test's tests/,
# userland/ and the like) or are what this build itself makes; nothing another test or an older build made.
#
#   plan/tools/guest/test-image.sh [--no-harness] CONFIG BUILD [ARGUMENT...]
#
# Each ARGUMENT is one of:
#   --file DEST=SOURCE    a file to copy into the image (two words)
#   --mode DEST=MODE      its permissions (two words)
#   NAME=VALUE            a make variable (ZEDBSD_TEST_RC_CONF=, ZEDBSD_TEST_IMAGE_TAG=, ...)
#   TARGET                a make target (default disk-image)
# The guest harness's files (the SSH keys and net.conf, plan/tools/guest/guest.py extra-files) are added unless
# --no-harness is given.  ZEDBSD_JOBS sets the parallel jobs (plan/tools/guest/jobs.sh, default 16).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/guest/jobs.sh
harness=y
if [ "${1:-}" = --no-harness ]; then
	harness=n
	shift
fi
[ $# -ge 2 ] || { echo "usage: test-image.sh [--no-harness] CONFIG BUILD [ARGUMENT...]" >&2; exit 2; }
config=$1
build=$2
shift 2
[ -f "$config" ] || { echo "test-image: no config $config" >&2; exit 1; }
files=
if [ $harness = y ]; then
	files=$(python3 plan/tools/guest/guest.py extra-files | sed -n "s/^ZEDBSD_TEST_EXTRA_FILES='\(.*\)'$/\1/p")
	[ -n "$files" ] || { echo "test-image: no guest harness files (plan/tools/guest/guest.py keys?)" >&2; exit 1; }
fi
variables=
targets=
while [ $# -gt 0 ]; do
	case $1 in
	--file|--mode)
		[ $# -ge 2 ] || { echo "test-image: $1 needs DEST=VALUE" >&2; exit 2; }
		files="$files $1 $2"
		shift 2
		;;
	*=*)
		variables="$variables
$1"
		shift
		;;
	*)
		targets="$targets $1"
		shift
		;;
	esac
done
[ -n "$targets" ] || targets=disk-image
# The make variables are passed one per line, so a value may hold spaces.
set -- make -j"$ZEDBSD_JOBS" ZEDBSD_CONFIG="$config" BUILD="$build" "ZEDBSD_TEST_EXTRA_FILES=$files"
old_ifs=$IFS
IFS='
'
for variable in $variables; do
	set -- "$@" "$variable"
done
IFS=$old_ifs
# shellcheck disable=SC2086
exec "$@" $targets
