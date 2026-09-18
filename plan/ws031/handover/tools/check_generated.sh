#!/bin/sh
# WS031: the generated reference ports must equal what their generators produce NOW from the fixed references.
# Detects a hand edit of a generated file, a generator change that was not re-run, and a reference that changed.
# Regenerates into a scratch directory and compares; nothing under src/ is written.  Run from the repository root.
set -eu
R=plan/ws031/linux-parity/linux-reference
T=plan/ws031/handover/tools
S=src/drivers/gpu/i915/parity
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
rc=0

compare() {   # <label> <generated dir> <source dir> <files...>
	label=$1; gen=$2; src=$3; shift 3
	for f in "$@"; do
		if cmp -s "$gen/$f" "$src/$f"; then
			echo "same      $label/$f"
		else
			echo "DIFFERENT $label/$f"
			rc=1
		fi
	done
}

mkdir -p "$W/vbt" "$W/dp" "$W/lcd"
python3 $T/port_intel_bios.py $R/ubu-i915-src/display "$W/vbt" > /dev/null
compare vbt "$W/vbt" $S/vbt intel_bios_port.c intel_vbt_defs.h intel_bios.h vbt_ref_types.h
python3 $T/port_dp_aux_pps.py $R/ubu-i915-src/display $R/drm-v6.8.12 "$W/dp" > /dev/null
compare dp "$W/dp" $S/dp intel_dp_aux_port.c intel_pps_port.c drm_dp_helper_port.c drm_edid_port.c \
	intel_dp_aux_regs.h intel_pps_regs.h intel_pps.h intel_dp_aux.h drm_dp.h dp_ref_types.h
python3 $T/port_lcd_calc.py $R/ubu-i915-src $R/drm-v6.8.12 "$W/lcd" > /dev/null
# every file the LCD generator writes (the list grows with tools/port_lcd_modeset.json)
compare lcd "$W/lcd" $S/lcd $(ls "$W/lcd")
python3 $T/gen_dp_fixture.py plan/ws031/display-ref "$W/dp_fixture.h" > /dev/null
if cmp -s "$W/dp_fixture.h" $S/dp/dp_fixture_latitude5330.h; then echo "same      dp/dp_fixture_latitude5330.h"; else echo "DIFFERENT dp/dp_fixture_latitude5330.h"; rc=1; fi

# the fixed DRM reference files themselves
( cd $R/drm-v6.8.12 && sha256sum -c SHA256SUMS ) || rc=1
[ $rc -eq 0 ] && echo "check_generated: all generated files are reproducible from the fixed references" || echo "check_generated: MISMATCH"
exit $rc
