#!/usr/bin/env python3
"""WS031 E-122 round 66: N0's "adopted by the parser" also covers the OpRegion source (the bytes P2 copied and the
parser consumed).  usage: round66.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)
pr = open(P + "probe.c").read()
pr = rep(pr, "static uint64_t n0_gmadr_base, n0_gmadr_size;",
         "static uint64_t n0_gmadr_base, n0_gmadr_size;" + NL +
         "static const struct parity_opregion_data *p2_opd;   /* E-122: the P2 OpRegion data (read-only acquisition) */")
pr = rep(pr, "\t\t\tparity_opregion_log(&opd);" + NL, "\t\t\tparity_opregion_log(&opd);" + NL + "\t\t\tp2_opd = &opd;" + NL)
pr = rep(pr, """		nd.parser_size = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_size : 0u;
		nd.parser_sha256 = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_sha256 : 0;""",
         """		nd.parser_size = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_size : 0u;
		nd.parser_sha256 = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_sha256 : 0;
		if (vbt_state.source == PARITY_VBT_SRC_OPREGION && p2_opd != 0 && p2_opd->vbt_valid) {
			nd.parser_size = p2_opd->vbt_size;          /* the copy P2 made is what the parser consumed */
			nd.parser_sha256 = p2_opd->vbt_sha256;
		}""")
open(P + "probe.c", "w").write(pr)
print("done")
