#!/bin/sh
# WS031: the generated files of src/drivers/gpu/i915 must equal what their generators produce NOW from the fixed inputs.
# Detects a hand edit of a generated file, a generator change that was not re-run, and an input that changed.
# Regenerates into a scratch directory and compares; nothing under src/ is written.  Run from the repository root.
#
# Checked: intel/forcewake-ranges.inc (gen_fw_ranges.py), render/vulkan-codec.inc (gen_vk_server_codec.py),
# and the fixed DRM reference files (SHA256SUMS).
# Not checked here:
#   - the display ports (port_lcd_calc.py, port_dp_aux_pps.py, port_intel_bios.py): retired in the rebuild
#     (i915-rebuild-s4.md section 9-6); the display definitions in intel/ are maintained by hand
#     and the extraction manifest (intel/provenance/) was deleted on 2026-09-22 (user decision).  See tools/retired/README.md.
#   - the GT headers of intel/ (gt-regs.h, commands.h, lrc-offsets.h, pci-ids.h, mocs.h and the rest): they hold
#     the old gen-inc.py outputs (i915-regs, -ids, -commands, -lrc-offsets, from the Linux v6.19 tree, which is not
#     kept on the build host) with the hand transcriptions; gen-inc.py is retired (tools/retired/README.md) and
#     the headers are maintained by hand.
#   - tests/display/dp-fixture-latitude5330.h: rewritten for the new tree in S5; gen_dp_fixture.py still emits the
#     old parity layout, so it no longer reproduces the file.
set -eu
R=plan/ws031/linux-parity/linux-reference
T=plan/ws031/handover/tools
S=src/drivers/gpu/i915
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
rc=0

compare() {   # <label> <generated file> <checked-in file>
	if cmp -s "$2" "$3"; then
		echo "same      $1"
	else
		echo "DIFFERENT $1"
		rc=1
	fi
}

python3 $T/gen_fw_ranges.py "$W/forcewake-ranges.inc" > /dev/null
compare intel/forcewake-ranges.inc "$W/forcewake-ranges.inc" $S/intel/forcewake-ranges.inc

# gen_vk_server_codec.py reads and writes under the root it is given: hand it a scratch root holding codec.c.
mkdir -p "$W/root/userland/base/libvulkan" "$W/root/$S/render"
cp userland/base/libvulkan/codec.c "$W/root/userland/base/libvulkan/"
python3 $T/gen_vk_server_codec.py "$W/root" > /dev/null
compare render/vulkan-codec.inc "$W/root/$S/render/vulkan-codec.inc" $S/render/vulkan-codec.inc

# the fixed DRM reference files themselves
( cd $R/drm-v6.8.12 && sha256sum -c --quiet SHA256SUMS ) || rc=1
[ $rc -eq 0 ] && echo "check_generated: all generated files are reproducible from the fixed inputs" || echo "check_generated: MISMATCH"
exit $rc
