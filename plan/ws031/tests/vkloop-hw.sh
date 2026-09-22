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
#        plan/ws031/tests/vkloop-hw.sh oracle 2500     the same for the frame of shader time 2500 ms (--time-ms)
#        plan/ws031/tests/vkloop-hw.sh display [ms]    E-129: on the panel (no --offscreen), the frame of shader time ms
#                                                      (default 2500) held 20 s for the camera; "display live": 12 s of animation
#        plan/ws031/tests/vkloop-hw.sh "-DFOO=1"       extra CPPFLAGS
#        plan/ws031/tests/vkloop-hw.sh "oracle -DI915_VK_REFERENCE_KERNELS=1"   (flags after the word)
#        plan/ws031/tests/vkloop-hw.sh wayland         the zwl compositor and two wltest clients (FIFO 600 frames, then mailbox
#                                                      with a swapchain recreate) in place of vkdemo; services in plan/ws031/tests/wayland/
#        plan/ws031/tests/vkloop-hw.sh test <scenario>  the i915 test build (I915_TESTS=y) running one scenario of
#                                                      src/drivers/gpu/i915/tests/execution/runner.c after the device
#                                                      start (e.g. ktest, eu, draw, r1, tex, t3, bl, lcdb, display_ktest);
#                                                      prints the test lines, then the usual lines of the vkdemo run;
#                                                      vkdemo starts TEST_WAIT_S (default 90) seconds later than
#                                                      usual, so a long scenario ends before the application runs
#        plan/ws031/tests/vkloop-hw.sh "test <scenario> -DFOO=1"             (flags after the scenario)
#        Every run takes the machine: flock /tmp/i915-hw.lock plan/ws031/tests/vkloop-hw.sh ...
set -u
cd "$(dirname "$0")/../../.."
EXTRA=${1:-}
TIME_MS=${2:-}
ORACLE=0
DISPLAY_RUN=0
SCENARIO=
case "$EXTRA" in test*)
	set -- ${EXTRA#test} $TIME_MS
	SCENARIO=${1:-}
	[ -n "$SCENARIO" ] || { echo "usage: $0 test <scenario>"; exit 2; }
	shift
	I915_TESTS=y
	EXTRA="-DI915_TEST_SCENARIO=$SCENARIO $*"
	TIME_MS= ;;
esac
WAYLAND_RUN=0
case "$EXTRA" in wayland*)
	WAYLAND_RUN=1
	EXTRA="${EXTRA#wayland}" ;;
esac
case "$EXTRA" in display*)
	DISPLAY_RUN=1
	EXTRA="${EXTRA#display}"
	TIME_MS=${TIME_MS:-2500} ;;
esac
case "$EXTRA" in oracle*)
	ORACLE=1
	I915_TESTS=y
	I915_TEST_ORACLE=y
	EXTRA="${EXTRA#oracle}" ;;
esac
# XXX: the run is the QEMU passthrough, whose guest sees no OpRegion, so the kernel always carries the
# test machine's captured VBT (I915_TEST_VBT=y, vendor/intel-vbt/).  Drop it once the GPU tests run on bare metal.
I915_TEST_VBT=y
mkdir -p build/resident
# a change of flags or of the test build is not seen by make: force the rebuild and relink by hand
FLAGS="$EXTRA|${I915_TESTS:-n}|${I915_TEST_ORACLE:-n}|vbt=$I915_TEST_VBT"
[ -f build/resident/.vkloop-flags ] && [ "$(cat build/resident/.vkloop-flags)" = "$FLAGS" ] || {
	touch src/drivers/gpu/i915/i915.c
	# the scenario is read by the runner only
	[ ! -f src/drivers/gpu/i915/tests/execution/runner.c ] || touch src/drivers/gpu/i915/tests/execution/runner.c
	# the test VBT is read by these only
	touch src/drivers/gpu/i915/display/vbt.c src/drivers/gpu/i915/display/display.c
	[ ! -f src/drivers/gpu/i915/tests/execution/ktest-display.c ] || touch src/drivers/gpu/i915/tests/execution/ktest-display.c
}
# the probe service as this run wants it; the file changes (and the cached image is rebuilt) only when its text does
PROBE=build/resident/vkprobe1.gen
if [ "$DISPLAY_RUN" = 1 ] && [ "$TIME_MS" = live ]; then
	sed "s/--offscreen --readback --token=vk1 --duration=1/--token=vk1 --duration=${LIVE_S:-12}/" plan/ws031/tests/vkprobe1 > $PROBE.new
elif [ "$DISPLAY_RUN" = 1 ]; then
	sed "s/--offscreen --readback --token=vk1 --duration=1/--readback --token=vk1 --time-ms=$TIME_MS --hold=20/" plan/ws031/tests/vkprobe1 > $PROBE.new
elif [ -n "$TIME_MS" ]; then
	sed "s/--duration=1/--time-ms=$TIME_MS/" plan/ws031/tests/vkprobe1 > $PROBE.new
else
	cp plan/ws031/tests/vkprobe1 $PROBE.new
fi
cmp -s $PROBE.new $PROBE 2>/dev/null || mv $PROBE.new $PROBE
# the first wait as this run wants it: a test scenario runs before the node is served, so the application waits longer
WAIT1=build/resident/vkwait1.gen
if [ -n "$SCENARIO" ]; then
	sed "s/^arguments=45\$/arguments=$((45 + ${TEST_WAIT_S:-90}))/" plan/ws031/tests/vkwait1 > $WAIT1.new
else
	cp plan/ws031/tests/vkwait1 $WAIT1.new
fi
cmp -s $WAIT1.new $WAIT1 2>/dev/null || mv $WAIT1.new $WAIT1
FILES="--file /etc/service.d/vkprobe1=$PROBE --file /etc/service.d/vkwait1=$WAIT1"
for n in vkwait2 poweroff; do
	FILES="$FILES --file /etc/service.d/$n=plan/ws031/tests/$n"
done
RC_CONF=plan/ws031/tests/vkprobe-rc.conf
if [ "$WAYLAND_RUN" = 1 ]; then
	# the compositor and its clients take the place of vkdemo: the display lease is exclusive
	FILES="--file /etc/service.d/vkwait1=$WAIT1 --file /etc/service.d/poweroff=plan/ws031/tests/poweroff"
	for n in zwl wlwait wltest1 wltest2 vkwait2; do
		FILES="$FILES --file /etc/service.d/$n=plan/ws031/tests/wayland/$n"
	done
	RC_CONF=plan/ws031/tests/wayland/rc.conf
fi
# the image is rebuilt only when an input is newer than it: switching to an older rc.conf does not
# count, so the chosen one is copied to one path whose file changes only when its text does
RC_GEN=build/resident/rc-conf.gen
cp "$RC_CONF" $RC_GEN.new
cmp -s $RC_GEN.new $RC_GEN 2>/dev/null || mv $RC_GEN.new $RC_GEN
rm -f $RC_GEN.new
RC_CONF=$RC_GEN
make -j"$(nproc)" BUILD=build/resident "I915_TESTS=${I915_TESTS:-n}" "I915_TEST_ORACLE=${I915_TEST_ORACLE:-n}" \
	"I915_TEST_VBT=$I915_TEST_VBT" \
	"ZEDBSD_TEST_CPPFLAGS=-DGPU_IOCTL_TRACE=1 $EXTRA" \
	ZEDBSD_TEST_RC_CONF=$RC_CONF "ZEDBSD_TEST_EXTRA_FILES=$FILES" \
	ZEDBSD_TEST_IMAGE_TAG=vkprobe disk-image > /tmp/resident-build.log 2>&1 || {
	echo "BUILD FAILED (the image on the 5330 is NOT this tree):"
	grep -E ' error: |Error [0-9]' /tmp/resident-build.log | head
	exit 1
}
printf '%s' "$FLAGS" > build/resident/.vkloop-flags
# the iGPU goes back to vfio-pci if a Venus run left it on the host i915 driver (bigbang/igpu-mode.sh)
ssh solaris10-man bigbang/igpu-mode.sh vfio >/dev/null || { echo "iGPU is not on vfio-pci"; exit 1; }
scp -q build/resident/hdd-image.img solaris10-man:bigbang/guest-parity.img || exit 1
ssh solaris10-man 'cd ~/bigbang && rm -f run-parity-serial.log && ./run-parity-vk.sh >/dev/null 2>&1; cp run-parity-serial.log vkloop-last.log'
scp -q solaris10-man:bigbang/vkloop-last.log /tmp/vkloop-last.log
if [ -n "$SCENARIO" ]; then
	echo "--- test $SCENARIO"
	# the runner's line, the suite tally and skips, and every verdict line a scenario logs
	grep -aE 'i915: (test |ktest|MCR-PROBE summary)|i915: .*(verdict|[A-Z0-9-]+ (PASS|FAIL|HANG|ERROR)[:( ]|[A-Z0-9-]+ cleanup|[A-Z0-9-]+ release:)' /tmp/vkloop-last.log | cut -c1-300 | head -100
	echo "--- vkdemo"
fi
grep -anE 'i915: vk|gpu: ioctl|VKDEMO|vkdemo:|ZWL|WLTEST|wltest:|zwl:|resident|panic|fault|init: ' /tmp/vkloop-last.log | grep -v 'parity N0\|parity P\|expected_fault' | cut -c1-200 | head -60
if [ "$ORACLE" = 1 ]; then
	python3 plan/ws031/handover/tools/vkdump_verify.py /tmp/vkloop-last.log plan/ws014/tests /tmp/vkframe1.ppm "${TIME_MS:-0}"
fi
