#!/bin/sh
# WS031 E-127: one hardware iteration of the libvulkan connectivity loop.
# Builds the resident image with the vkprobe service chain, runs it on the 5330 (ssh alias
# solaris10-man) and prints what the application, the executor and the GPU core said.
#
# usage: plan/ws031/tests/vkloop-hw.sh                 one run; the log lines that matter
#        plan/ws031/tests/vkloop-hw.sh oracle          the same with the first frame dumped over serial,
#                                                      rebuilt and given to the independent pixel oracle
#                                                      (plan/ws014/tests/vkdemo_oracle.py).  The dump is slow:
#                                                      the application times out after it, by design.
#        plan/ws031/tests/vkloop-hw.sh "-DFOO=1"       extra CPPFLAGS
set -u
cd "$(dirname "$0")/../../.."
EXTRA=${1:-}
ORACLE=0
if [ "$EXTRA" = oracle ]; then
	ORACLE=1
	EXTRA="-DI915_VK_GFX_DUMP=1"
fi
# make does not know the flags changed: the one file they reach is rebuilt by hand
[ -f build/resident/.vkloop-flags ] && [ "$(cat build/resident/.vkloop-flags)" = "$EXTRA" ] ||
	touch src/drivers/gpu/i915/vk/gfx-draw.c
FILES=""
for n in vkwait1 vkprobe1 vkwait2 poweroff; do
	FILES="$FILES --file /etc/service.d/$n=plan/ws031/tests/$n"
done
make -j"$(nproc)" BUILD=build/resident CONFIG_DRIVER_PCI_I915_PARITY=y \
	"ZEDBSD_TEST_CPPFLAGS=-DPARITY_RESIDENT=1 -DPARITY_RESIDENT_SERVE_S=150 -DPARITY_RESIDENT_STOP_ON_CLOSE=1 -DGPU_IOCTL_TRACE=1 $EXTRA" \
	ZEDBSD_TEST_RC_CONF=plan/ws031/tests/vkprobe-rc.conf "ZEDBSD_TEST_EXTRA_FILES=$FILES" \
	ZEDBSD_TEST_IMAGE_TAG=vkprobe disk-image > /tmp/resident-build.log 2>&1 || {
	echo "BUILD FAILED (the image on the 5330 is NOT this tree):"
	grep -E ' error: |Error [0-9]' /tmp/resident-build.log | head
	exit 1
}
printf '%s' "$EXTRA" > build/resident/.vkloop-flags
scp -q build/resident/hdd-image.img solaris10-man:bigbang/guest-parity.img || exit 1
ssh solaris10-man 'cd ~/bigbang && rm -f run-parity-serial.log && ./run-parity-vk.sh >/dev/null 2>&1; cp run-parity-serial.log vkloop-last.log'
scp -q solaris10-man:bigbang/vkloop-last.log /tmp/vkloop-last.log
grep -anE 'i915: vk|gpu: ioctl|VKDEMO|vkdemo:|resident|panic|fault|init: ' /tmp/vkloop-last.log | grep -v 'parity N0\|parity P\|expected_fault' | cut -c1-200 | head -60
if [ "$ORACLE" = 1 ]; then
	python3 plan/ws031/handover/tools/vkdump_verify.py /tmp/vkloop-last.log plan/ws014/tests /tmp/vkframe1.ppm 0
fi
