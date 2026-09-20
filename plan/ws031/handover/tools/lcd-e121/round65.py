#!/usr/bin/env python3
"""WS031 E-122 round 65: OpRegion as DATA only (VBT_ONLY), per the expert's decision (option C).
 - acquisition: P2 maps the OpRegion READ-ONLY, copies it, locates the VBT (RVDA / mailbox #4), copies + validates it.
 - adoption: the parser's source order follows intel_opregion_get_vbt(): the explicit blob (vbt_firmware) first, then
   the OpRegion VBT, then the PCI ROM.  Consumed bytes are hashed whatever the source.
 - runtime: DISABLED (reason ACPI_RUNTIME_UNAVAILABLE): no notifier, no drdy / ardy / chpd / csts / didl / cadl write
   (the mapping is read-only, so a write is impossible by construction), no ASLE worker.  The firmware's values are
   observed and logged only.
 - the probe no longer stops at intel_opregion_register for this reason.
 usage: round65.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
BS = chr(92)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---------------- the pure locator also reads the runtime mailboxes (observation) ----------------
h = open(P + "opregion_vbt.h").read()
h = rep(h, """	uint32_t vbt_offset, vbt_max;   /* mailbox #4: offset and the size the reference allows */
};""", """	uint32_t vbt_offset, vbt_max;   /* mailbox #4: offset and the size the reference allows */
	/* runtime mailboxes as the firmware (or a previous driver) left them -- OBSERVED, never written by this driver */
	uint32_t acpi_drdy, acpi_csts, acpi_cevt, acpi_chpd, acpi_clid;   /* struct opregion_acpi at 0x100 */
	uint32_t asle_ardy, asle_aslc, asle_tche;                         /* struct opregion_asle at 0x300 */
};""")
open(P + "opregion_vbt.h", "w").write(h)
c = open(P + "opregion_vbt.c").read()
c = rep(c, """	out->mboxes = le32(op + 0x58);""", """	out->mboxes = le32(op + 0x58);
	/* struct opregion_acpi (0x100): drdy 0x00, csts 0x04, cevt 0x08, chpd 0xa8, clid 0xac */
	out->acpi_drdy = le32(op + 0x100);
	out->acpi_csts = le32(op + 0x104);
	out->acpi_cevt = le32(op + 0x108);
	out->acpi_chpd = le32(op + 0x1a8);
	out->acpi_clid = le32(op + 0x1ac);
	/* struct opregion_asle (0x300): ardy 0x00, aslc 0x04, tche 0x08 */
	out->asle_ardy = le32(op + 0x300);
	out->asle_aslc = le32(op + 0x304);
	out->asle_tche = le32(op + 0x308);""")
open(P + "opregion_vbt.c", "w").write(c)

# ---------------- kernel: the P2 data acquisition (read-only) ----------------
nh = open(P + "native_precheck.h").read()
nh = rep(nh, "/* fills *r; returns r->proceed */", r"""/*
 * The OpRegion as DATA (VBT_ONLY, E-122): P2 maps it read-only, copies the 8 KiB and the VBT it names, validates the
 * VBT.  The runtime protocol (ACPI notifier, drdy / ardy / chpd / csts / DIDL / CADL, ASLE) is NOT joined: runtime =
 * DISABLED, reason ACPI_RUNTIME_UNAVAILABLE.  The mailboxes' current values are observed only.
 */
struct parity_opregion_data {
	int present;                    /* ASLS != 0 */
	int mapped;
	struct parity_opregion_info op;
	int vbt_valid;
	const uint8_t *vbt;             /* the copy handed to the parser (0 = none) */
	uint32_t vbt_size;
	uint8_t vbt_sha256[32];
	int runtime_enabled;            /* always 0 here */
	const char *runtime_reason;
};
int parity_opregion_read_data(uint32_t asls, struct parity_opregion_data *out);
void parity_opregion_log(const struct parity_opregion_data *d);

/* fills *r; returns r->proceed */""")
open(P + "native_precheck.h", "w").write(nh)
n = open(P + "native_precheck.c").read()
n = rep(n, "static void read_vtd(const struct parity_native_deps *d, struct parity_native_report *r)", r"""int
parity_opregion_read_data(uint32_t asls, struct parity_opregion_data *out)
{
	static uint8_t opcopy[PARITY_OPREGION_SIZE];
	static uint8_t vbtcopy[16384];
	void *op = 0, *vb = 0;
	unsigned i;

	memset(out, 0, sizeof(*out));
	out->runtime_enabled = 0;
	out->runtime_reason = "ACPI_RUNTIME_UNAVAILABLE (no AML / ACPI event delivery in zedBSD): the OpRegion is used as data only";
	out->present = asls != 0u;
	if (!out->present)
		return 0;
	/* READ-ONLY view: this driver cannot write the shared mailboxes through it */
	if (hal_space_map_device((hal_physaddr_t)asls, PARITY_OPREGION_SIZE, HAL_SPACE_READ, &op) != HAL_OK || op == 0)
		return -1;
	out->mapped = 1;
	for (i = 0u; i < PARITY_OPREGION_SIZE; i++)
		opcopy[i] = ((const volatile uint8_t *)op)[i];
	(void)hal_space_unmap_device(op, PARITY_OPREGION_SIZE);
	if (parity_opregion_locate_vbt(opcopy, sizeof(opcopy), asls, &out->op) != 0)
		return -1;
	if (out->op.src == PARITY_OPVBT_RVDA) {
		uint32_t len = out->op.rvds;

		if (len > sizeof(vbtcopy) || out->op.rvda_inside)
			return -1;
		if (hal_space_map_device((hal_physaddr_t)out->op.vbt_phys, len, HAL_SPACE_READ, &vb) != HAL_OK || vb == 0)
			return -1;
		for (i = 0u; i < len; i++)
			vbtcopy[i] = ((const volatile uint8_t *)vb)[i];
		(void)hal_space_unmap_device(vb, len);
		out->vbt_size = len;
	} else {
		out->vbt_size = out->op.vbt_max;
		for (i = 0u; i < out->vbt_size; i++)
			vbtcopy[i] = opcopy[out->op.vbt_offset + i];
	}
	out->vbt_valid = parity_vbt_validate(vbtcopy, out->vbt_size);
	if (out->vbt_valid) {
		out->vbt = vbtcopy;
		parity_sha256(vbtcopy, out->vbt_size, out->vbt_sha256);
	}
	return 0;
}

void
parity_opregion_log(const struct parity_opregion_data *d)
{
	if (!d->present) {
		kern_logf("i915: parity P2 opregion: absent (ASLS=0): no OpRegion data, no runtime protocol\n");
		return;
	}
	kern_logf("i915: parity P2 opregion: data=%s ver=%u.%u mboxes=0x%x VBT via %s size=%u valid=%d | runtime=DISABLED (%s)\n",
		d->mapped && d->op.signature_ok ? "AVAILABLE" : "UNAVAILABLE", d->op.major, d->op.minor, d->op.mboxes,
		d->op.src == PARITY_OPVBT_RVDA ? "RVDA" : d->op.src == PARITY_OPVBT_MAILBOX4 ? "mailbox#4" : "none", d->vbt_size,
		d->vbt_valid, d->runtime_reason);
	kern_logf("i915: parity P2 opregion runtime mailboxes as found (observed only; this driver writes none of them): "
		"ACPI drdy=0x%x csts=0x%x cevt=0x%x chpd=0x%x clid=0x%x | ASLE ardy=0x%x aslc=0x%x tche=0x%x\n", d->op.acpi_drdy,
		d->op.acpi_csts, d->op.acpi_cevt, d->op.acpi_chpd, d->op.acpi_clid, d->op.asle_ardy, d->op.asle_aslc, d->op.asle_tche);
}

static void read_vtd(const struct parity_native_deps *d, struct parity_native_report *r)""")
n = n.replace("\\n\"", BS + "n\"")   # no-op guard (raw strings keep the backslash)
open(P + "native_precheck.c", "w").write(n)

# ---------------- bios: take the OpRegion bytes, in the reference's order ----------------
b = open(P + "bios.c").read()
b = rep(b, """	if (opregion_has_vbt)
		vbt->source = PARITY_VBT_SRC_OPREGION;   /* present but not lifted: no bytes */

	if (vbt_buf == 0 && explicit_blob && explicit_blob_get(vbt, pci, &vbt_buf, &vbt_size)) {
		vbt->source = PARITY_VBT_SRC_EXPLICIT_BLOB;
		origin = PARITY_VBT_ORIGIN_EXPLICIT_BLOB;
	}""", """	/*
	 * E-122: intel_opregion_get_vbt() order -- the firmware file (vbt_firmware; here the explicit blob) first, then the
	 * OpRegion VBT that P2 copied and validated (RVDA / mailbox #4), then the PCI ROM.
	 */
	(void)opregion_has_vbt;
	if (vbt_buf == 0 && explicit_blob && explicit_blob_get(vbt, pci, &vbt_buf, &vbt_size)) {
		vbt->source = PARITY_VBT_SRC_EXPLICIT_BLOB;
		origin = PARITY_VBT_ORIGIN_EXPLICIT_BLOB;
	}
	if (vbt_buf == 0 && opregion_vbt_buf != 0 && parity_vbt_validate(opregion_vbt_buf, opregion_vbt_size)) {
		vbt_buf = opregion_vbt_buf;
		vbt_size = opregion_vbt_size;
		vbt->source = PARITY_VBT_SRC_OPREGION;
		origin = PARITY_VBT_ORIGIN_OPREGION;
	}""")
b = rep(b, """int
parity_intel_bios_init_ex(struct parity_vbt_state *vbt, struct osdep_pci *pci,""",
        """/* E-122: the OpRegion VBT copy P2 made (read-only acquisition); 0 = none */
static const void *opregion_vbt_buf;
static size_t opregion_vbt_size;

void
parity_bios_set_opregion_vbt(const void *buf, size_t size)
{
	opregion_vbt_buf = buf;
	opregion_vbt_size = size;
}

int
parity_intel_bios_init_ex(struct parity_vbt_state *vbt, struct osdep_pci *pci,""")
# the line before the definition must not keep a lone return type: check the original declaration form
open(P + "bios.c", "w").write(b)
bh = open(P + "bios.h").read()
bh = rep(bh, "const uint8_t *parity_vbt_explicit_pin(void);", "const uint8_t *parity_vbt_explicit_pin(void);" + NL +
         "/* E-122: the OpRegion VBT (P2's read-only copy) offered to intel_bios_init, after the explicit blob */" + NL +
         "void parity_bios_set_opregion_vbt(const void *buf, size_t size);")
open(P + "bios.h", "w").write(bh)
# ---------------- probe: P2 acquisition, no stop at intel_opregion_register ----------------
pr = open(P + "probe.c").read()
pr = rep(pr, """		kern_logf("i915: parity P2 opregion: ASLS=0x%08x\n", asls);""", """		kern_logf("i915: parity P2 opregion: ASLS=0x%08x\n", asls);
		/* E-122 VBT_ONLY: the OpRegion as data (read-only copy + VBT); the runtime protocol is not joined */
		{
			static struct parity_opregion_data opd;

			(void)parity_opregion_read_data(asls, &opd);
			parity_opregion_log(&opd);
			if (opd.vbt_valid) {
				parity_bios_set_opregion_vbt(opd.vbt, opd.vbt_size);
				opregion_vbt_present = 1;
			}
		}""")
pr = rep(pr, """	if (opregion_present) {
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_UNIMPL,
			"intel_opregion_register", 0u, 0u);
		res.outcome = PARITY_BLOCKED;
		res.where = "intel_opregion_register";
		goto teardown;
	}
	parity_i915_driver_register(&dprobe, &dcore, &probe_pm, opregion_present);""", """	/*
	 * intel_opregion_register(): E-122 VBT_ONLY -- the OpRegion runtime protocol is not joined (no ACPI notifier is
	 * registered, drdy / ardy / csts / DIDL / CADL are not written, no ASLE worker), as in the reference's build
	 * without ACPI where these calls are empty.  Recorded; the probe continues.
	 */
	if (opregion_present) {
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_NOTE,
			"intel_opregion_register:runtime_disabled(vbt_only)", 0u, 0u);
		kern_logf("i915: parity P7 intel_opregion_register: runtime DISABLED (VBT_ONLY, ACPI_RUNTIME_UNAVAILABLE): no "
			"notifier registered, no mailbox written, no ASLE service -- the probe continues\n");
	}
	parity_i915_driver_register(&dprobe, &dcore, &probe_pm, 0 /* opregion runtime not registered */);""")
open(P + "probe.c", "w").write(pr)
pc = open(P + "native_precheck.c").read()
pc = rep(pc, """(r->conditions & PARITY_N0_C_OPREGION_REGISTER) ? " OPREGION_REGISTER(later wall: not ported)" : "",""",
         """(r->conditions & PARITY_N0_C_OPREGION_REGISTER) ? " OPREGION_PRESENT(data only; runtime disabled)" : "",""")
open(P + "native_precheck.c", "w").write(pc)
print("done")
