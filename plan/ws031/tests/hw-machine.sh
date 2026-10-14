# The machines the i915 passthrough tests run on (sourced by plan/ws031/tests/vkloop-hw.sh, plan/ws075/tests/test-hw.sh
# and plan/ws101/tests/hw/run-hw.sh).  I915_MACHINE picks one (default 5330); a variable already set in the
# environment wins over the machine's value.  Each machine keeps the same ~/bigbang/ (run-parity-vk.sh and
# igpu-mode.sh, plan/ws031/tests/host/) and runs the guest with its iGPU 0000:00:02.0 on vfio-pci.
#
#   5330  Dell Latitude 5330, i5-1245U, Gen12 (8086:46a8); the ssh alias solaris10-man; no boot word.  The guest sees
#         no OpRegion and the kernel no longer carries the machine's captured VBT (removed 2026-10-14, ws233-p001),
#         so the driver takes the Linux missing-VBT defaults.  plan/tools/hw5330/README.md
#   sv7   Panasonic Let's Note SV7, Core i5 8th generation, Gen9.5 UHD 620 (8086:5917); awe@10.0.10.27.  The driver
#         has no Gen9 display (the picture stays on the firmware's framebuffer); the boot word
#         i915.gen9=on is kept though Gen9 is taken by default since 2026-10-14 (ws229-p005).  plan/tools/hwsv7/README.md
#
# Sets I915_MACHINE_HOST (the machine's ssh name; each script decides whether I915_HOST overrides it) and exports
# I915_MACHINE, I915_IGPU (vendor:device the run checks on the host before it starts, empty: no check),
# and I915_BOOT_EXTRA (boot words for ZEDBSD_BOOT_EXTRA_LINES, empty: none).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
case "${I915_MACHINE:=5330}" in
5330)
	I915_MACHINE_HOST=solaris10-man
	: "${I915_IGPU:=}"
	: "${I915_BOOT_EXTRA:=}"
	;;
sv7)
	I915_MACHINE_HOST=awe@10.0.10.27
	: "${I915_IGPU:=8086:5917}"
	: "${I915_BOOT_EXTRA:=i915.gen9=on}"
	;;
*)
	echo "I915_MACHINE=$I915_MACHINE: unknown (5330 or sv7)" >&2
	exit 2
	;;
esac
export I915_MACHINE I915_IGPU I915_BOOT_EXTRA
