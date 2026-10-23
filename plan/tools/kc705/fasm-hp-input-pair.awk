# Works around an openXC7 FASM bug for a right-hand HP I/O tile (RIOB18,
# not SING) whose two halves are both single-ended LVCMOS12/15/18 inputs:
# nextpnr-himbaechel 3e5c2cdd (himbaechel/uarch/xilinx/fasm.cc,
# write_io_config, is_right_hp_single_ended_input_master) gives the P half
# IOB_Y0 the feature LVCMOS12_LVCMOS15.IN, whose prjxray segbits also clear
# bit 39_01, the bit IOB_Y1's .IN sets; it gives IOB_Y1 no .IN at all, and
# pulls IOB_Y1 down as if it were unused.  On the KC705 the N half
# (AB12, GPIO_SW_S) then read 1 in the fabric while boundary scan read its
# pad as 0 (ws288 phase001).  This rewrites such a tile so that each half
# carries its own LVCMOS12_LVCMOS15_LVCMOS18.IN (38_126 39_127 on IOB_Y0,
# 38_00 39_01 on IOB_Y1; the two do not overlap) and drops the stray
# pulldown of IOB_Y1.  A half that also drives (DRIVE) or is SSTL or
# differential is left alone.
#   awk -f fasm-hp-input-pair.awk IN.fasm IN.fasm > OUT.fasm   (two passes)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

function tile_of(s) { return substr(s, 1, index(s, ".") - 1) }

FNR == NR {
	if ($0 !~ /^RIOB18_X[0-9]+Y[0-9]+\.IOB_Y[01]\./)
		next
	t = tile_of($0)
	h = substr($0, length(t) + 2, 6)
	f = substr($0, length(t) + 9)
	if (f ~ /\.IN_ONLY$/)
		in_only[t, h] = 1
	if (f ~ /DRIVE|SSTL12_SSTL135_SSTL15\.IN$|IN_DIFF/)
		other[t, h] = 1
	if (h == "IOB_Y0" && f == "LVCMOS12_LVCMOS15.IN")
		master_in[t] = 1
	next
}

FNR == 1 {
	for (k in master_in)
		if (in_only[k, "IOB_Y0"] && in_only[k, "IOB_Y1"] &&
		    !other[k, "IOB_Y0"] && !other[k, "IOB_Y1"])
			fix[k] = 1
}

{
	t = tile_of($0)
	if (!(t in fix)) {
		print
		next
	}
	if ($0 == t ".IOB_Y0.LVCMOS12_LVCMOS15.IN" ||
	    $0 == t ".IOB_Y1.PULLTYPE.PULLDOWN" ||
	    $0 == t ".IOB_Y0.LVCMOS12_LVCMOS15_LVCMOS18.IN" ||
	    $0 == t ".IOB_Y1.LVCMOS12_LVCMOS15_LVCMOS18.IN")
		next
	print
}

END {
	for (k in fix) {
		print k ".IOB_Y0.LVCMOS12_LVCMOS15_LVCMOS18.IN"
		print k ".IOB_Y1.LVCMOS12_LVCMOS15_LVCMOS18.IN"
		printf "fasm-hp-input-pair: %s: each half its own .IN\n", k > "/dev/stderr"
	}
}
