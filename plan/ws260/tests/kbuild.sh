#!/bin/sh
# The three kernel builds every stage of the display 9 rewrite passes with no warning: the amd64 default, and the
# i915 test kernels of the display and display_ktest sets.  Each goes to its own directory under build/tmp/.
#   sh plan/ws260/tests/kbuild.sh NAME
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
name=${1:?name}
C=config/ci/config-amd64.mk
status=0
for set in default vke2 execution; do
	B=build/tmp/ws260-k-$name-$set
	if [ "$set" = default ]; then
		extra=
	else
		extra="I915_TESTS=y I915_TEST_SET=$set"
	fi
	# shellcheck disable=SC2086
	if make -j16 ZEDBSD_CONFIG=$C BUILD=$B $extra $B/vmunix > $B.log 2>&1; then
		echo "kbuild $set: rc 0, warnings $(grep -c 'warning:' $B.log)"
	else
		echo "kbuild $set: FAILED (see $B.log)"
		status=1
	fi
done
exit $status
