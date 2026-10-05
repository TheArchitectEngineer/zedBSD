#!/bin/sh
# ws031-p024: the compiler's boundaries on the host.  Each GLSL file of this directory says in its second line
# whether the i915 compiler must accept or refuse it; it is compiled with glslc (-O0, and -O for the accepted
# ones) and handed to the parser and code generator by plan/ws068/tests/i915-shader-check.  The refusals must
# name their reason; nothing may crash (the run is under ASan and UBSan).
#
#   plan/ws031/tests/p024/run.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
here=$(cd "$(dirname -- "$0")" && pwd)
root=$here/../../../..
out=${1:-$root/build/ws031-p024}
mkdir -p "$out"
status=0
cc -std=gnu99 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=all -I"$root" -I"$root/include" \
	-DHAL_ARCH_AMD64 -o "$out/check" "$root/plan/ws068/tests/i915-shader-check/main.c" -lm || exit 1

# Each file with its expected outcome and, for a refusal, the reason's words.
check() {
	file=$1 expect=$2 reason=$3 flags=$4
	name=$(basename "$file" .frag)$flags
	glslc --target-env=vulkan1.0 $flags -fshader-stage=fragment -o "$out/$name.spv" "$file" || {
		echo "$name: FAIL glslc"; status=1; return; }
	result=$(ASAN_OPTIONS=detect_leaks=0 "$out/check" fragment "$out/$name.spv" 2>&1)
	case $expect in
	accept)
		if echo "$result" | grep -q ": accepted"; then
			echo "$name: accepted"
		else
			echo "$name: FAIL $result"; status=1
		fi ;;
	refuse)
		if echo "$result" | grep -q "REFUSED.*$reason"; then
			echo "$name: refused ($reason)"
		else
			echo "$name: FAIL $result"; status=1
		fi ;;
	esac
}

check "$here/loops8.frag" accept "" -O0
check "$here/loops9.frag" refuse "loops nested too deep" -O0
check "$here/ifs31.frag" accept "" -O0
check "$here/ifs40.frag" refuse "constructs nested too deep" -O0
for shader in noinput divzero shift killoop; do
	check "$here/$shader.frag" accept "" -O0
	check "$here/$shader.frag" accept "" -O
done

[ $status -eq 0 ] && echo "ws031-p024 boundary host test PASS" || echo "ws031-p024 boundary host test FAIL"
exit $status
