/*
 * WS031 Linux-parity -- see opregion_vbt.h.  zedBSD project code.
 */
#include "opregion_vbt.h"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

int
parity_opregion_locate_vbt(const uint8_t *op, size_t len, uint64_t asls, struct parity_opregion_info *out)
{
	static const char sig[16] = { 'I','n','t','e','l','G','r','a','p','h','i','c','s','M','e','m' };
	const uint8_t *asle;
	unsigned i;

	if (op == 0 || out == 0 || len < PARITY_OPREGION_SIZE)
		return -1;
	for (i = 0u; i < sizeof(*out); i++)
		((uint8_t *)out)[i] = 0u;
	for (i = 0u; i < 16u; i++)
		if (op[i] != (uint8_t)sig[i])
			return -1;
	out->signature_ok = 1;
	out->size_kib = le32(op + 16);
	out->revision = op[21];          /* struct { u8 rsvd, revision, minor, major } over at 0x14 */
	out->minor = op[22];
	out->major = op[23];
	out->mboxes = le32(op + 0x58);
	/* struct opregion_acpi (0x100): drdy 0x00, csts 0x04, cevt 0x08, chpd 0xa8, clid 0xac */
	out->acpi_drdy = le32(op + 0x100);
	out->acpi_csts = le32(op + 0x104);
	out->acpi_cevt = le32(op + 0x108);
	out->acpi_chpd = le32(op + 0x1a8);
	out->acpi_clid = le32(op + 0x1ac);
	/* struct opregion_asle (0x300): ardy 0x00, aslc 0x04, tche 0x08 */
	out->asle_ardy = le32(op + 0x300);
	out->asle_aslc = le32(op + 0x304);
	out->asle_tche = le32(op + 0x308);
	asle = op + PARITY_OPREGION_ASLE_OFFSET;
	out->rvda = le64(asle + 186);    /* struct opregion_asle: rvda after ... fdss(u64) fdsp stat */
	out->rvds = le32(asle + 194);
	/* opregion->asle exists when MBOX_ASLE (bit 2) is set */
	if (out->major >= 2u && (out->mboxes & 0x4u) != 0u && out->rvda != 0u && out->rvds != 0u) {
		out->rvda_relative = out->major > 2u || out->minor >= 1u;
		out->rvda_inside = out->rvda_relative && out->rvda < PARITY_OPREGION_SIZE;
		out->src = PARITY_OPVBT_RVDA;
		out->vbt_phys = out->rvda_relative ? asls + out->rvda : out->rvda;
		return 0;
	}
	/* mailbox #4: up to the ASLE ext mailbox when that one is in use, else to the end of the OpRegion */
	out->src = PARITY_OPVBT_MAILBOX4;
	out->vbt_offset = PARITY_OPREGION_VBT_OFFSET;
	out->vbt_max = ((out->mboxes & 0x10u) != 0u ? PARITY_OPREGION_ASLE_EXT : PARITY_OPREGION_SIZE) - PARITY_OPREGION_VBT_OFFSET;
	return 0;
}
