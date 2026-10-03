#!/usr/bin/env python3
"""WS031 E-121 round 61: N0 record per the E-120 review.
 - pipe state in four classes: POWER_OFF (valid read of the wells' STATE), READ_ERROR (all-ones where a register value
   must be), READABLE_INACTIVE, READABLE_ACTIVE; a READ_ERROR is never rounded to inactive (it stops).
 - GSTS decoded (TES, RTPS, FLS, AFLS, WBFS, QIES, IRES, IRTPS, CFIS); IRES is recorded as a condition for the MSI path
   (not a DMA stop).
 - primary stop reason + every observed condition (a bitmask), incl. the known later wall: ASLS != 0 -> the probe's
   intel_opregion_register (not ported).
 - VBT: observed (N0, source + sha) vs adopted by the parser (source + sha of the consumed bytes) and "same bytes".
 usage: round61.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---------------- header ----------------
h = open(P + "native_precheck.h").read()
h = rep(h, """struct parity_native_pipe {
	int readable;                   /* pipe or transcoder power domain on */""", """enum parity_native_pipe_class {
	PARITY_N0_POWER_OFF = 0,        /* the wells' STATE bits read validly as off: the pipe cannot run */
	PARITY_N0_READ_ERROR,           /* the power state or a pipe register did not read as a register value */
	PARITY_N0_READABLE_INACTIVE,
	PARITY_N0_READABLE_ACTIVE
};

/* every condition N0 observed (primary_stop is the first in decision order) */
#define PARITY_N0_C_ACTIVE_PIPE     (1u << 0)
#define PARITY_N0_C_PIPE_READ_ERROR (1u << 1)
#define PARITY_N0_C_GGTT_OVERLAP    (1u << 2)
#define PARITY_N0_C_VTD_UNREADABLE  (1u << 3)
#define PARITY_N0_C_VTD_TRANSLATION (1u << 4)
#define PARITY_N0_C_VTD_PMR         (1u << 5)
#define PARITY_N0_C_VTD_IR_ENABLED  (1u << 6)   /* interrupt remapping on: the MSI path must be checked (not a DMA stop) */
#define PARITY_N0_C_OPREGION_REGISTER (1u << 7) /* ASLS != 0: the probe stops later at intel_opregion_register (not ported) */
#define PARITY_N0_C_VBT_DIFFERS     (1u << 8)   /* the OpRegion VBT is not the bytes the parser consumed */

struct parity_native_pipe {
	int readable;                   /* pipe or transcoder power domain on */
	int cls;                        /* enum parity_native_pipe_class */""")
h = rep(h, """	int overlap;
	int proceed;
	const char *reason;
};""", """	int overlap;
	int proceed;
	const char *reason;
	/* E-121 */
	uint32_t pwr_well_ctl;          /* the driver request register (STATE bits) as read: all-ones = READ_ERROR */
	uint32_t conditions;            /* PARITY_N0_C_* observed */
	uint32_t primary_stop;          /* the one that decided STOP (0 when PROCEED) */
	int parser_src;                 /* enum parity_vbt_source the parser consumed */
	uint32_t parser_size;
	uint8_t parser_sha256[32];
	int parser_sha_known, vbt_same_bytes;
};""")
h = rep(h, """	const uint8_t *vbt_pin;         /* the explicit blob's pinned sha256 (may be 0) */
};""", """	const uint8_t *vbt_pin;         /* the explicit blob's pinned sha256 (may be 0) */
	/* what the VBT parser actually consumed (intel_bios_init ran before N0) */
	int parser_src;
	uint32_t parser_size;
	const uint8_t *parser_sha256;   /* 0 when not known */
};""")
open(P + "native_precheck.h", "w").write(h)

# ---------------- precheck: power classes, pipe read error, VBT pairing ----------------
c = open(P + "native_precheck.c").read()
c = rep(c, """	for (p = 0u; p < 4u; p++) {
		struct parity_native_pipe *q = &r->pipe[p];
		uint32_t o = 0x1000u * p;

		q->readable = d->pipe_powered(d->ctx, p);
		if (!q->readable) {
			r->unreadable_pipes |= 1u << p;
			continue;
		}""", """	/* HSW_PWR_WELL_CTL2: the STATE bits the power decision reads; all-ones is not a register value */
	r->pwr_well_ctl = osdep_mmio_read32(m, 0x45404u);
	for (p = 0u; p < 4u; p++) {
		struct parity_native_pipe *q = &r->pipe[p];
		uint32_t o = 0x1000u * p;

		if (r->pwr_well_ctl == 0xffffffffu) {
			q->cls = PARITY_N0_READ_ERROR;
			r->unreadable_pipes |= 1u << p;
			continue;
		}
		q->readable = d->pipe_powered(d->ctx, p);
		if (!q->readable) {
			q->cls = PARITY_N0_POWER_OFF;
			r->unreadable_pipes |= 1u << p;
			continue;
		}""")
c = rep(c, """		if ((q->transconf & 0x80000000u) != 0u || (q->plane_ctl & 0x80000000u) != 0u)
			r->active_pipes |= 1u << p;""", """		if (q->transconf == 0xffffffffu || q->plane_ctl == 0xffffffffu) {
			q->cls = PARITY_N0_READ_ERROR;
			continue;
		}
		q->cls = PARITY_N0_READABLE_INACTIVE;
		if ((q->transconf & 0x80000000u) != 0u || (q->plane_ctl & 0x80000000u) != 0u) {
			q->cls = PARITY_N0_READABLE_ACTIVE;
			r->active_pipes |= 1u << p;
		}""")
c = rep(c, """	read_display(d, r);""", """	read_display(d, r);
	r->parser_src = d->parser_src;
	r->parser_size = d->parser_size;
	if (d->parser_sha256 != 0) {
		unsigned i;

		r->parser_sha_known = 1;
		r->vbt_same_bytes = r->vbt_valid;
		for (i = 0u; i < 32u; i++) {
			r->parser_sha256[i] = d->parser_sha256[i];
			if (r->vbt_sha256[i] != d->parser_sha256[i])
				r->vbt_same_bytes = 0;
		}
	}""")
# logging: power class names, GSTS decode, VBT pairing, conditions
c = rep(c, """			kern_logf("i915: parity N0 pipe %c: not_readable""", """			kern_logf("i915: parity N0 pipe %c: %s -- not_readable""")
c = rep(c, """concludes; its registers were not read)\\n", 'A' + (int)p);""", """concludes; its registers were not read)\\n", 'A' + (int)p,
				q->cls == PARITY_N0_READ_ERROR ? "READ_ERROR (the power state did not read as a register value; NOT counted "
				"inactive)" : "POWER_OFF");""")
c = rep(c, """		kern_logf("i915: parity N0 pipe %c: TRANSCONF=""", """		kern_logf("i915: parity N0 pipe %c: %s\\n", 'A' + (int)p, q->cls == PARITY_N0_READABLE_ACTIVE ? "READABLE_ACTIVE" :
			q->cls == PARITY_N0_READ_ERROR ? "READ_ERROR (a pipe register read all-ones)" : "READABLE_INACTIVE");
		kern_logf("i915: parity N0 pipe %c: TRANSCONF=""")
c = rep(c, """	kern_logf("i915: parity N0 firmware framebuffer:""", """	kern_logf("i915: parity N0 vt-d GSTS decoded: TES=%u RTPS=%u FLS=%u AFLS=%u WBFS=%u QIES=%u IRES=%u IRTPS=%u CFIS=%u | "
		"PMEN EPM=%u PRS=%u%s\\n", (r->vtd_gsts >> 31) & 1u, (r->vtd_gsts >> 30) & 1u, (r->vtd_gsts >> 29) & 1u,
		(r->vtd_gsts >> 28) & 1u, (r->vtd_gsts >> 27) & 1u, (r->vtd_gsts >> 26) & 1u, (r->vtd_gsts >> 25) & 1u,
		(r->vtd_gsts >> 24) & 1u, (r->vtd_gsts >> 23) & 1u, r->vtd_pmen & 1u, (r->vtd_pmen >> 31) & 1u,
		r->vtd_readable ? "" : " (not register values)");
	kern_logf("i915: parity N0 VBT: observed %s %u bytes sha256=%s.. | adopted by the parser: %s %u bytes sha256=%s.. | "
		"same bytes=%d\\n", r->op.src == PARITY_OPVBT_RVDA ? "OPREGION(RVDA)" : r->op.src == PARITY_OPVBT_MAILBOX4 ?
		"OPREGION(mailbox#4)" : "none", r->vbt_size, r->vbt_mapped ? sha : "-",
		r->parser_src == PARITY_VBT_SRC_EXPLICIT_BLOB ? "EXPLICIT_BLOB" : r->parser_src == PARITY_VBT_SRC_OPREGION ? "OPREGION" :
		r->parser_src == PARITY_VBT_SRC_PCI_ROM ? "PCI_ROM" : "NONE", r->parser_size, r->parser_sha_known ? psha : "-",
		r->vbt_same_bytes);
	kern_logf("i915: parity N0 firmware framebuffer:""")
c = rep(c, """	sha[16] = '\\0';""", """	sha[16] = '\\0';
	for (i = 0u; i < 8u; i++) {
		static const char hx2[] = "0123456789abcdef";
		psha[2u * i] = hx2[r->parser_sha256[i] >> 4];
		psha[2u * i + 1u] = hx2[r->parser_sha256[i] & 15u];
	}
	psha[16] = '\\0';""")
c = rep(c, """	char sha[17];""", """	char sha[17], psha[17];""")
c = rep(c, """	kern_logf("i915: parity N0 decision: %s -- %s (active pipes 0x%x, unreadable 0x%x)\\n", r->proceed ? "PROCEED" : "STOP before "
		"any display write", r->reason, r->active_pipes, r->unreadable_pipes);""", """	kern_logf("i915: parity N0 decision: %s -- %s (active pipes 0x%x, unreadable 0x%x)\\n", r->proceed ? "PROCEED" : "STOP before "
		"any display write", r->reason, r->active_pipes, r->unreadable_pipes);
	kern_logf("i915: parity N0 conditions: primary=0x%x observed=0x%x [%s%s%s%s%s%s%s%s%s]\\n", r->primary_stop, r->conditions,
		(r->conditions & PARITY_N0_C_ACTIVE_PIPE) ? " ACTIVE_PIPE" : "",
		(r->conditions & PARITY_N0_C_PIPE_READ_ERROR) ? " PIPE_READ_ERROR" : "",
		(r->conditions & PARITY_N0_C_GGTT_OVERLAP) ? " GGTT_OVERLAP" : "",
		(r->conditions & PARITY_N0_C_VTD_UNREADABLE) ? " VTD_UNREADABLE" : "",
		(r->conditions & PARITY_N0_C_VTD_TRANSLATION) ? " VTD_TRANSLATION" : "",
		(r->conditions & PARITY_N0_C_VTD_PMR) ? " VTD_PMR" : "",
		(r->conditions & PARITY_N0_C_VTD_IR_ENABLED) ? " VTD_IR_ENABLED(check the MSI path)" : "",
		(r->conditions & PARITY_N0_C_OPREGION_REGISTER) ? " OPREGION_REGISTER(later wall: not ported)" : "",
		(r->conditions & PARITY_N0_C_VBT_DIFFERS) ? " VBT_DIFFERS" : "");""")
open(P + "native_precheck.c", "w").write(c)

# ---------------- decision: all conditions + the primary ----------------
dsrc = open(P + "native_decide.c").read()
a = dsrc.index("	r->proceed = 0;")
b = dsrc.index("}", dsrc.index("	else {", a))
b = dsrc.index("}", b + 1) + 1          # end of the else-block
dsrc = dsrc[:a] + r"""	/* every condition (so one native run names every wall it can see), then the primary in decision order */
	r->conditions = 0u;
	if (r->active_pipes != 0u)
		r->conditions |= PARITY_N0_C_ACTIVE_PIPE;
	for (p = 0u; p < 4u; p++)
		if (r->pipe[p].cls == PARITY_N0_READ_ERROR)
			r->conditions |= PARITY_N0_C_PIPE_READ_ERROR;
	if (r->overlap)
		r->conditions |= PARITY_N0_C_GGTT_OVERLAP;
	if (!r->hypervisor && r->vtd_enabled) {
		if (!r->vtd_readable)
			r->conditions |= PARITY_N0_C_VTD_UNREADABLE;
		else {
			if ((r->vtd_gsts & 0x80000000u) != 0u)
				r->conditions |= PARITY_N0_C_VTD_TRANSLATION;
			if ((r->vtd_pmen & 0x80000001u) != 0u)
				r->conditions |= PARITY_N0_C_VTD_PMR;
			if ((r->vtd_gsts & 0x02000000u) != 0u)
				r->conditions |= PARITY_N0_C_VTD_IR_ENABLED;
		}
	}
	if (r->asls != 0u)
		r->conditions |= PARITY_N0_C_OPREGION_REGISTER;
	if (r->vbt_valid && r->parser_sha_known && !r->vbt_same_bytes)
		r->conditions |= PARITY_N0_C_VBT_DIFFERS;

	r->proceed = 0;
	r->primary_stop = 0u;
	if (r->conditions & PARITY_N0_C_ACTIVE_PIPE) {
		r->primary_stop = PARITY_N0_C_ACTIVE_PIPE;
		r->reason = "a pipe is active (firmware display): the takeover (N1: readout + crtc_disable_noatomic) is not ported";
	} else if (r->conditions & PARITY_N0_C_PIPE_READ_ERROR) {
		r->primary_stop = PARITY_N0_C_PIPE_READ_ERROR;
		r->reason = "a pipe's power state / registers did not read as register values: not shown inactive";
	} else if (r->conditions & PARITY_N0_C_GGTT_OVERLAP) {
		r->primary_stop = PARITY_N0_C_GGTT_OVERLAP;
		r->reason = "the firmware scanout overlaps the GGTT pages this driver writes";
	} else if (r->conditions & PARITY_N0_C_VTD_UNREADABLE) {
		r->primary_stop = PARITY_N0_C_VTD_UNREADABLE;
		r->reason = "the GPU's VT-d unit is enabled but its status is not readable";
	} else if (r->conditions & (PARITY_N0_C_VTD_TRANSLATION | PARITY_N0_C_VTD_PMR)) {
		r->primary_stop = r->conditions & (PARITY_N0_C_VTD_TRANSLATION | PARITY_N0_C_VTD_PMR);
		r->reason = "the GPU's VT-d unit translates / protects memory (firmware DMA protection) and zedBSD has no IOMMU driver";
	} else {
		r->proceed = 1;
		r->reason = r->hypervisor ? "start conditions match the prepared path (no active pipe, no overlap; VT-d: guest view, "
			"the host owns the unit)" : "start conditions match the prepared path (no active pipe, no overlap, DMA untranslated)";
	}
""" + dsrc[b:]
open(P + "native_decide.c", "w").write(dsrc)

# ---------------- probe: pass what the parser consumed ----------------
pr = open(P + "probe.c").read()
pr = rep(pr, """		nd.vbt_pin = parity_vbt_explicit_pin();""", """		nd.vbt_pin = parity_vbt_explicit_pin();
		/* what intel_bios_init (P3) actually handed to the parser; only the explicit blob's bytes are hashed today */
		nd.parser_src = vbt_state.source;
		nd.parser_size = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_size : 0u;
		nd.parser_sha256 = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_sha256 : 0;""")
open(P + "probe.c", "w").write(pr)
print("done")
