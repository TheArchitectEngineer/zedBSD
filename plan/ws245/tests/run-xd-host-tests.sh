#!/bin/sh
# ws245: the host tests of the xd display (display 11 to 13, src/drivers/gpu/i915/display/xd-*.c) on the own register
# model (src/drivers/gpu/i915/tests/display/xd-fake-regs.c), under ASan/UBSan, with the lock order record.
#   sh plan/ws245/tests/run-xd-host-tests.sh [OUT]   (default build/ws245-host)
# Each test is built with the display host library (every display source as production, host-kernel.c's stand-ins;
# plan/ws084/tests/display-host-lib.sh).  The tests:
#   host-xd-regs-test      the register headers against the tree's older headers (pairs of xd-regs-map.h)
#   host-xd-io-test        the register window: access, waits, the write record, the mailbox, the lock order
#   host-xd-platform-test  the platform descriptions: PCI ids, steppings, PCH ids, the description rows
#   host-xd-vbt-test       the VBT, OpRegion and EDID decode on made-up tables, against the old parser on Alder Lake-P
#   host-xd-survey-test    the survey of the first picture on the register model, and the start's record of the display
#                          writes (src/drivers/gpu/i915/mmio-record.c)
#   host-xd-aux-test       the AUX channel: the control word, native and I2C-over-AUX transactions through the data
#                          words, the sink's replies, the tries and waits of the channel's faults; the native DPCD
#                          reads and writes (the wake-up read, messages of 16 bytes, retries)
#   host-xd-gmbus-test     the GMBUS controller: EDID reads with an offset and the segment pointer, the command words,
#                          the interrupt enables, the target's refusals and the controller's faults
#   host-xd-panel-test     the panel: the backlight PWM's period, lowest duty, scaling, controllers, start and stop;
#                          the power sequencer's delays, VDD alone, power on and off and their waits
#   host-xd-power-test     the power wells: the Alder Lake-P wells against the 5330's list, the writes of power well 1,
#                          a pipe well, the Type-C and Thunderbolt AUX wells, drains, BIOS requests; the counts:
#                          which wells power which functions, their order, the deferred give-back, the on record;
#                          the DC states: the verified write, DC off, DC9
#   host-xd-dmc-test       the DMC firmware: the entry for the stepping, the copies, the refusals, the writes of a
#                          load (Alder Lake-P and Tiger Lake), the pipe DMC switch
#   host-xd-clock-test     the CD clock: the read, the check that keeps it, the start, a crawl and a relock, the
#                          stop, the firmware's refusals, the voltage levels
#   host-xd-dpll-test      the combo port PLLs: the DisplayPort, HDMI and Thunderbolt words and their link clocks, the
#                          writes of an enable and a disable, the AFC startup, the late waits, the read, the DDI clock map
#   host-xd-ddi-test       the combo PHYs: the check of a PHY brought up (the 5330's PHY A), the start and the stop, the
#                          lanes a port powers, the swing table of each output, the signal levels of the 5330's eDP
#   host-xd-dp-test        the DisplayPort link: the rates both ends offer, the fallback, the choice of a link, the
#                          M/N values, the link status, the training against a model sink and repeaters
#   host-xd-pipe-test      the transcoders and pipes: the 5330's panel against its firmware's words, the order of a
#                          setup, an HDMI mode of display 12, the start and the stop, the pipe's own words
#   host-xd-dbuf-test      the display buffer and watermarks: the 5330's panel against the driver's run, the slices
#                          and joining, the shares, the levels the blank and the allocation cut, the slices' power,
#                          the bus's joining, the data box credits
#   host-xd-core-test      the display core's start and stop: the order on Alder Lake-P and Tiger Lake, the buddies,
#                          the faults that stop a start
#   host-xd-takeover-test  the firmware's display: the decision on each start situation, the readout of an
#                          Alder Lake-P (which writes nothing), the output's kind by the ports, the framebuffer's pages;
#                          the takeover's stop of a lit pipe in the disable order, and the VGA plane's; the sanitize
#                          of what the firmware left unused (clocks, PLLs, the compressor); the display's takeover
#                          of the lit panel (backlight, sink to sleep, panel power)
#   host-xd-screen-test    a screen's plan: the 5330's panel against the older check's numbers (CD clock, bandwidth,
#                          level 0, the plane's and the cursor's blocks), the watermarks (pipes sharing a slice by
#                          weight, a blank too short, a share too small), the refusals (CD clock, voltage, QGV, sinks,
#                          links), HDMI and Type-C DisplayPort links, Tiger Lake's pixel rate rule; an update that
#                          lands in one frame (the evasion window, the scan line, a line slept out, a blank that does
#                          not come, a refused interrupt reference, writes that cross a blank)
#   host-xd-output-test    a screen's output: the 5330's panel lit (power uses in order, the display buffer, the
#                          backlight from the VBT, the PLL, the port and its trained link, the pipe, the start) and
#                          darkened in reverse; the backlight's levels; a PLL that does not lock, a link that does not
#                          train, a refused power reference; an HDMI sink on DDI B; a Type-C DisplayPort sink in
#                          DisplayPort alternate and Thunderbolt mode; Ice Lake's panel on the eDP transcoder and a
#                          TC1 sink on the MG PHY (display 11's order of the clock select and the port's transport)
#   host-xd-edp-test       the built-in panel's sink: the sequencer's choice and delays, VDD held across the taking
#                          and let go five power cycles after its last use, back on after the power cycle, the power
#                          references and locks, the panel's steps and waits, the stop, the extended capabilities,
#                          the refusals of the taking, the DPCD retries, the EDID over the AUX channel's I2C
#   host-xd-tc-test        the Type-C PHYs and PLLs: the Dekel windows, bank index and lock, another route, the PHY
#                          microcontroller's health, the lanes' DisplayPort mode, the swing tables and words with the load
#                          generators, the soft reset; the PLL words (the 5330's TC PLL 2, every rate and reference, HDMI)
#                          and link clocks, the enable and the read of a TC PLL; Tiger Lake's TC cold well; a port's
#                          live state, FIA, lanes and pin; the connect and disconnect of the FIA flow (Tiger Lake) and
#                          of the TCSS flow (display 13)
#   host-xd-cover-test     every platform row: found by its PCI id, the links (eDP, HDMI, the first Type-C connector)
#                          planned with PLL words that give them, a CD clock step, the firmware's output classified
#   host-xd-icl-test       display 11 (Ice Lake): the rows, the MG PLL (Linux's words, the enable's order and masks, the
#                          read), the MG PHY's DisplayPort mode and swing, the single FIA, TC cold and the connections, the
#                          AUX wells (the TC cold exit, the combo AUX lane, WA #1178), the eDP transcoder, the port's
#                          transport, the CD clock's bypass, the Thunderbolt PLL, the data box, a version 1 DMC package
#   host-xd-picture-test   the pictures of a display: a prepare's refusals before anything is written; the 5330's panel
#                          prepared, lit, flipped, its brightness and backlight, darkened, with the status at each step
#                          and the power, PLL and locks given back; a second screen on the other combo PLL or sharing
#                          one, none left; a dark not confirmed, the retained screen and the model's discard; a TC1 sink
#                          reserving its port's PLL and the Thunderbolt PLL
#   host-xd-hpd-test       the hotplug: the pins of the ports, the arming of the north and south hotplug registers
#                          (the 5330's ports; ports A and B with the interrupts on and off), the pulses'
#                          acknowledgment and length, their sorting, a storm's mask and rearming, an HDMI monitor's
#                          detection over GMBUS, a Type-C DisplayPort sink's through the hooks, the retries, the pulses
#   host-output-link-test  an output's planned link (display/output.c): the 5330's USB-C monitor at the link Linux
#                          trained, the fallback's limits, a branch's depth and dot clock, an HDMI mode's link and PLL;
#                          the firmware's output by the platform's ports
# The check against the Linux tree itself is plan/ws245/tests/check-xd-regs-linux.sh (needs the unpacked tree).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
. plan/ws084/tests/display-host-lib.sh
T=src/drivers/gpu/i915/tests/display
OUT=${1:-build/ws245-host}
mkdir -p "$OUT"
I915_HOST_EXTRA_PRODUCTION="src/drivers/gpu/i915/perf.c ${I915_HOST_EXTRA_PRODUCTION:-}"
failed=0
for test in host-xd-regs-test host-xd-io-test host-xd-platform-test host-xd-vbt-test host-xd-survey-test host-xd-aux-test host-xd-gmbus-test host-xd-panel-test host-xd-power-test host-xd-dmc-test host-xd-clock-test host-xd-dpll-test host-xd-ddi-test host-xd-dp-test host-xd-pipe-test host-xd-dbuf-test host-xd-core-test host-xd-takeover-test host-xd-screen-test host-xd-output-test host-xd-edp-test host-xd-tc-test host-xd-cover-test host-xd-icl-test host-xd-picture-test host-xd-hpd-test host-output-link-test; do
	test -f "$T/$test.c" || continue
	i915_display_host_build "$OUT/$test" "$T/$test.c" "$T/xd-fake-regs.c" plan/ws084/tests/native-decide-stubs.c
	if ! "$OUT/$test"; then
		failed=1
	fi
done
# The xd files may not reach internal.h, the older display code's header, even through another header: each is
# compiled with I915_XD_STRICT, which internal.h refuses.
for source in src/drivers/gpu/i915/display/xd-*.c; do
	if ! ${CC:-cc} -std=gnu11 -DKERN_USER_ABI_LP64 -Iinclude -Isrc -I. -idirafter libc/include -DI915_XD_STRICT \
	    -fsyntax-only "$source"; then
		echo "run-xd-host-tests: $source reaches internal.h" >&2
		failed=1
	fi
done
# The steps of the xd code read the fields of a platform's description, never its kind or whether it was verified on
# a machine: neither is compared anywhere in the xd files.
if grep -nE '(->|\.)kind (==|!=) I915_XD_KIND_|(->|\.)verified\b' src/drivers/gpu/i915/display/xd-*.c; then
	echo "run-xd-host-tests: an xd file branches on the platform's kind or on verified" >&2
	failed=1
fi
if [ "$failed" -ne 0 ]; then
	echo "run-xd-host-tests: FAILED" >&2
	exit 1
fi
echo "run-xd-host-tests: all passed"
