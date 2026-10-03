#!/usr/bin/env python3
"""WS031 E-120 round 55: N0 fixes -- VT-d readability from its version register (a guest reads zeros there: that is a
guest view, not the unit); a pipe whose power domain is off is recorded not_readable and counted inactive (the
reference readout concludes the same: no power, no running crtc).  usage: round55.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/native_precheck.c"
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:80]
    s = s.replace(old, new)
rep("#define VTD_GSTS      0x1cu", "#define VTD_VER       0x00u                     /* version: non-zero on a real unit */\n#define VTD_GSTS      0x1cu")
rep("""	r->vtd_gsts = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_GSTS);""",
    """	r->vtd_ver = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_VER);
	r->vtd_gsts = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_GSTS);""")
rep("""	/* an all-ones read is not a register value (nothing decodes there, e.g. a guest) */
	r->vtd_readable = r->vtd_gsts != 0xffffffffu;""", """	/* a real unit reports its version (non-zero, not all-ones); zeros / all-ones = nothing decodes there (e.g. a guest's
	 * view of the host's address) -- then GSTS / PMEN are not register values */
	r->vtd_readable = r->vtd_ver != 0u && r->vtd_ver != 0xffffffffu;""")
rep("""	else if (r->unreadable_pipes != 0u && !r->hypervisor)
		r->reason = "a pipe's power domain is off: its state is not readable, so 'inactive' is not shown";
	else if""", "	else if")
rep("""		r->reason = "start conditions match the prepared path (no active pipe, no overlap, DMA untranslated)";""",
    """		r->reason = r->hypervisor ? "start conditions match the prepared path (no active pipe, no overlap; VT-d: guest view, "
			"the host owns the unit)" : "start conditions match the prepared path (no active pipe, no overlap, DMA untranslated)";""")
_a = s.index('kern_logf("i915: parity N0 vt-d (GPU unit)')
_b = s.index("r->vtd_pmen);", _a) + len("r->vtd_pmen);")
s = s[:_a] + 'kern_logf("i915: parity N0 vt-d (GPU unit): GFXVTBAR=0x%llx enabled=%d view=%s VER=0x%08x readable=%d GSTS=0x%08x "' + chr(10) + chr(9) + chr(9) + '"(TES bit31) PMEN=0x%08x (EPM bit0 / PRS bit31)' + chr(92) + 'n", (unsigned long long)r->gfxvtbar, r->vtd_enabled,' + chr(10) + chr(9) + chr(9) + 'r->hypervisor ? "guest" : "native", r->vtd_ver, r->vtd_readable, r->vtd_gsts, r->vtd_pmen);' + s[_b:]
_a = s.index('kern_logf("i915: parity N0 pipe %c: not_readable')
_b = s.index(");", _a) + 2
s = s[:_a] + 'kern_logf("i915: parity N0 pipe %c: not_readable (power domain off: counted inactive, as the reference readout "' + chr(10) + chr(9) + chr(9) + chr(9) + chr(9) + '"concludes; its registers were not read)' + chr(92) + 'n", ' + "'A' + (int)p);" + s[_b:]
open(p, "w").write(s)
h = root + "src/drivers/gpu/i915/parity/native_precheck.h"
t = open(h).read()
assert t.count("	uint32_t vtd_gsts, vtd_pmen;") == 1
t = t.replace("	uint32_t vtd_gsts, vtd_pmen;", "	uint32_t vtd_ver, vtd_gsts, vtd_pmen;")
t = t.replace(" * Decision: PROCEED only when (a) no pipe is active and no pipe is unreadable,", " * Decision: PROCEED only when (a) no pipe is active (a pipe whose power domain is off is not_readable and counted inactive),")
open(h, "w").write(t)
print("done")
