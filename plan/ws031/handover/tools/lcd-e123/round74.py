#!/usr/bin/env python3
"""WS031 E-123 round 74: the OPREGION-FW summary is repeated at the runner's end (next to the N0 summary), so it stays on
the screen of a native run.  usage: round74.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
BS = chr(92)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = open(L + "opregion_fwtest.c").read()
c = rep(c, "static uint32_t fw_bl_level;", "static uint32_t fw_bl_level;" + NL +
        "static int fw_ran, fw_rc;" + NL + "static unsigned fw_ok, fw_want;" + NL +
        "static uint32_t fw_seen[6];     /* after setup: CHPD ARDY | after register: DRDY ARDY | after unregister: DRDY ARDY */")
c = rep(c, "	log_mbox(\"after setup (CHPD 1, ARDY NOT_READY expected)\");",
        "	log_mbox(\"after setup (CHPD 1, ARDY NOT_READY expected)\");" + NL +
        "	fw_ran = 1;" + NL + "	fw_seen[0] = parity_opregion_mbox_read(0x1a8);" + NL + "	fw_seen[1] = parity_opregion_mbox_read(0x300);")
c = rep(c, "	log_mbox(\"after register (DIDL / CADL, CSTS 0, DRDY 1, TCHE 2, ARDY 1 expected)\");",
        "	log_mbox(\"after register (DIDL / CADL, CSTS 0, DRDY 1, TCHE 2, ARDY 1 expected)\");" + NL +
        "	fw_seen[2] = parity_opregion_mbox_read(0x100);" + NL + "	fw_seen[3] = parity_opregion_mbox_read(0x300);")
c = rep(c, "	ardy_reg = parity_opregion_mbox_read(0x300);" + NL + "	drdy_reg = parity_opregion_mbox_read(0x100);",
        "	ardy_reg = parity_opregion_mbox_read(0x300);" + NL + "	drdy_reg = parity_opregion_mbox_read(0x100);" + NL +
        "	fw_seen[4] = drdy_reg;" + NL + "	fw_seen[5] = ardy_reg;")
c = rep(c, "	return ok == want_ok && rc == 0 ? 0 : -1;" + NL + "}",
        "	fw_ok = ok;" + NL + "	fw_want = want_ok;" + NL + "	fw_rc = rc;" + NL +
        "	return ok == want_ok && rc == 0 ? 0 : -1;" + NL + "}" + NL + NL +
        "void parity_opregion_fw_log_again(void)" + NL + "{" + NL +
        "	if (!fw_ran)" + NL + "		return;" + NL +
        '	kern_logf("i915: parity OPREGION-FW summary (repeated): REAL OpRegion written | setup CHPD %u ARDY %u | register DRDY %u ARDY %u | "' + NL +
        '		"unregister DRDY %u ARDY %u | synthetic notify + ASLE answered | cleanup rc %d | verdict %s (%u/%u)' + BS + 'n", fw_seen[0], fw_seen[1],' + NL +
        "		fw_seen[2], fw_seen[3], fw_seen[4], fw_seen[5], fw_rc, fw_ok == fw_want && fw_rc == 0 ? \"PASS\" : \"FAIL\", fw_ok, fw_want);" + NL + "}")
open(L + "opregion_fwtest.c", "w").write(c)
h = open(L + "opregion_fwtest.h").read()
h = rep(h, "int parity_opregion_fw_test(uint32_t asls, const struct parity_vbt_state *vbt);",
        "int parity_opregion_fw_test(uint32_t asls, const struct parity_vbt_state *vbt);" + NL +
        "/* the summary once more (the runner's end), when the test ran */" + NL + "void parity_opregion_fw_log_again(void);")
open(L + "opregion_fwtest.h", "w").write(h)
r = open(P + "runner.c").read()
r = rep(r, '#include "native_precheck.h"' + NL, '#include "native_precheck.h"' + NL + '#include "lcd/opregion_fwtest.h"' + NL)
r = rep(r, "	parity_native_log_again();" + NL, "	parity_opregion_fw_log_again();" + NL + "	parity_native_log_again();" + NL)
open(P + "runner.c", "w").write(r)
print("done")
