#!/usr/bin/env python3
"""WS031 E-120 round 57: N0 reads the wells' real power state.  Before intel_power_domains_init_hw() the driver has
not synced (hw_enabled = -1) nor taken over the BIOS requests, so the readout gate would call every well off.  A
read-only helper answers from the STATE bits (any requester), with no write.  usage: round57.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
def rep(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)
c = open(P + "power_domains.c").read()
if "parity_power_domain_hw_state_on" not in c:
    c = rep(c, "/* ---- async put bookkeeping (intel_display_power.c) ---- */", """/*
 * N0 (E-120), read-only: are all of the domain's wells powered right now, whoever requested them (firmware / BIOS
 * request register or ours)?  The STATE bit of the well in the driver request register mirrors the well's actual
 * state; no request bit is read or written.  Always-on wells count as on; a DC_OFF well is not a power gate for
 * register access and is skipped.  Usable before intel_power_domains_init_hw() (no sync / takeover needed).
 */
int
parity_power_domain_hw_state_on(struct parity_power_domains *pd, enum parity_power_domain d, struct osdep_mmio *m)
{
	uint64_t wells = parity_power_domain_wells(pd, d);
	unsigned i = pd->num_power_wells;

	while (i-- > 0u) {
		struct parity_power_well *w;

		if ((wells & ((uint64_t)1u << i)) == 0u)
			continue;
		w = &pd->power_wells[i];
		if (w->always_on || w->ops == PARITY_PW_OPS_ALWAYS_ON || w->ops == PARITY_PW_OPS_DC_OFF)
			continue;
		if ((osdep_mmio_raw_read32(m, pw_driver_reg(w->ops)) & pw_state(w->hsw_idx)) == 0u)
			return 0;
	}
	return 1;
}

/* ---- async put bookkeeping (intel_display_power.c) ---- */""")
    open(P + "power_domains.c", "w").write(c)
h = open(P + "power_domains.h").read()
if "parity_power_domain_hw_state_on" not in h:
    h = rep(h, "int  parity_power_well_is_enabled(struct parity_power_well *w, struct parity_pw_ctx *c);",
            "int  parity_power_well_is_enabled(struct parity_power_well *w, struct parity_pw_ctx *c);\n"
            "/* N0: the domain's wells powered now per their STATE bits (any requester); read-only, usable before init_hw */\n"
            "int  parity_power_domain_hw_state_on(struct parity_power_domains *pd, enum parity_power_domain d, struct osdep_mmio *m);")
    open(P + "power_domains.h", "w").write(h)
p = open(P + "probe.c").read()
old = """	return parity_display_power_is_enabled(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pipe), n0_pwc) ||
		parity_display_power_is_enabled(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + pipe), n0_pwc);"""
if old in p:
    p = p.replace(old, """	/* the wells' real STATE (before init_hw the driver neither synced nor took over the firmware's requests) */
	return parity_power_domain_hw_state_on(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pipe), n0_mmio) &&
		parity_power_domain_hw_state_on(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + pipe), n0_mmio);""")
    p = rep(p, "static struct parity_pw_ctx *n0_pwc;", "static struct parity_pw_ctx *n0_pwc;\nstatic struct osdep_mmio *n0_mmio;")
    p = rep(p, "		n0_pwc = &pwc;", "		n0_pwc = &pwc;\n		n0_mmio = &mmio;")
    open(P + "probe.c", "w").write(p)
n = open(P + "native_precheck.c").read()
n = n.replace("not_readable (power domain off: counted inactive, as the reference readout \"\n\t\t\t\t\"concludes; its registers were not read)",
              "not_readable (its wells' STATE is off: counted inactive, as the reference readout \"\n\t\t\t\t\"concludes; its registers were not read)")
open(P + "native_precheck.c", "w").write(n)
print("done")
