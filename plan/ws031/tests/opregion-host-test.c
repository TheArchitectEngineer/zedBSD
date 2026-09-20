/*
 * WS031 E-120: the OpRegion VBT locator (parity/opregion_vbt.c) against the OpRegion the target laptop really has
 * (plan/ws031/display-ref/i915_opregion.bin, taken under Linux) and the VBT Linux extracted from it (i915_vbt.bin),
 * plus the reference decision variants.  zedBSD project code.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "opregion_vbt.h"

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static size_t slurp(const char *p, uint8_t *b, size_t cap)
{
	FILE *f = fopen(p, "rb");
	size_t n;

	if (f == 0)
		return 0;
	n = fread(b, 1, cap, f);
	fclose(f);
	return n;
}

int main(void)
{
	static uint8_t op[0x2000], v[16384], w[0x2000];
	struct parity_opregion_info in;
	const uint64_t asls = 0x614e5018u;              /* the ASLS read on the target (setpci 00:02.0 FC.L) */
	size_t n = slurp("plan/ws031/display-ref/i915_opregion.bin", op, sizeof(op));
	size_t nv = slurp("plan/ws031/display-ref/i915_vbt.bin", v, sizeof(v));
	uint32_t vbt_size = 0u, bdb = 0u;

	CHECK(n == 0x2000u && nv > 0u, "the target OpRegion (8 KiB) and its VBT are present");
	CHECK(parity_opregion_locate_vbt(op, n, asls, &in) == 0 && in.signature_ok && in.size_kib == 8u && in.major == 2u &&
	      in.minor == 1u, "TARGET: IntelGraphicsMem, 8 KiB, version 2.1");
	CHECK(in.mboxes == 0x1du && in.src == PARITY_OPVBT_RVDA && in.rvda == 0x2000u && in.rvds == 8704u && in.rvda_relative &&
	      !in.rvda_inside && in.vbt_phys == asls + 0x2000u,
	      "TARGET: the VBT is outside the OpRegion (RVDA 0x2000 relative, 8704 bytes) -> ASLS + 0x2000, not mailbox #4");
	memcpy(&vbt_size, v + 24, 2);
	memcpy(&bdb, v + 28, 4);
	CHECK(nv == in.rvds && memcmp(v, "$VBT", 4) == 0 && vbt_size <= nv && vbt_size > bdb && bdb < nv && memcmp(v + bdb, "BIOS_DATA_BLOCK", 15) == 0,
	      "TARGET: the VBT Linux read from there is RVDS bytes, its $VBT header declares a size that fits (8701 of 8704), BDB inside");
	printf("  target: ver %u.%u mboxes 0x%x rvda 0x%llx rvds %u -> phys 0x%llx | vbt %zu bytes, bdb at %u\n", in.major, in.minor,
	       in.mboxes, (unsigned long long)in.rvda, in.rvds, (unsigned long long)in.vbt_phys, nv, bdb);

	/* E-122: the runtime mailboxes are read for observation only (this dump was taken while Linux i915 ran: its values) */
	CHECK(in.acpi_drdy == 1u && in.acpi_csts == 0u && in.acpi_chpd == 1u && in.asle_ardy == 1u && in.asle_tche == 2u,
	      "MAILBOXES: struct opregion_acpi drdy/csts/chpd at 0x100/0x104/0x1a8 and struct opregion_asle ardy/tche at 0x300/0x308 "
	      "read as Linux left them (drdy=1, ardy=1, tche=BLC_EN)");
	printf("  mailboxes: drdy %u csts %u cevt %u chpd %u clid 0x%x | ardy %u aslc %u tche %u\n", in.acpi_drdy, in.acpi_csts,
	       in.acpi_cevt, in.acpi_chpd, in.acpi_clid, in.asle_ardy, in.asle_aslc, in.asle_tche);

	/* the reference's other branches, on a copy */
	memcpy(w, op, sizeof(w));
	memset(w + 0x300 + 186, 0, 12);                 /* no RVDA / RVDS */
	CHECK(parity_opregion_locate_vbt(w, sizeof(w), asls, &in) == 0 && in.src == PARITY_OPVBT_MAILBOX4 && in.vbt_offset == 0x400u &&
	      in.vbt_max == 0x1c00u - 0x400u, "MBOX4 no RVDA: mailbox #4, up to the ASLE-ext mailbox (MBOX_ASLE_EXT set)");
	w[0x58] &= (uint8_t)~0x10u;
	CHECK(parity_opregion_locate_vbt(w, sizeof(w), asls, &in) == 0 && in.vbt_max == 0x2000u - 0x400u,
	      "MBOX4 without the ASLE-ext mailbox: up to the end of the OpRegion");
	memcpy(w, op, sizeof(w));
	w[22] = 0u;                                     /* version 2.0 */
	CHECK(parity_opregion_locate_vbt(w, sizeof(w), asls, &in) == 0 && in.src == PARITY_OPVBT_RVDA && !in.rvda_relative &&
	      in.vbt_phys == 0x2000u, "V2.0: RVDA is a physical address (not added to ASLS)");
	memcpy(w, op, sizeof(w));
	w[0x300 + 186] = 0u;
	w[0x300 + 187] = 0x10u;                         /* rvda 0x1000 */
	CHECK(parity_opregion_locate_vbt(w, sizeof(w), asls, &in) == 0 && in.rvda_inside,
	      "INSIDE 2.1 RVDA inside the OpRegion is flagged (the reference WARNs)");
	memcpy(w, op, sizeof(w));
	w[0x58] &= (uint8_t)~0x4u;                      /* no ASLE mailbox: RVDA is not consulted */
	CHECK(parity_opregion_locate_vbt(w, sizeof(w), asls, &in) == 0 && in.src == PARITY_OPVBT_MAILBOX4,
	      "NO-ASLE: RVDA ignored without mailbox #3");
	memcpy(w, op, sizeof(w));
	w[0] = 'X';
	CHECK(parity_opregion_locate_vbt(w, sizeof(w), asls, &in) == -1, "BAD-SIG refused");
	CHECK(parity_opregion_locate_vbt(op, 0x1000u, asls, &in) == -1, "SHORT refused");
	printf("opregion_host_test: %d checks, %d failures\n", checks, fails);
	return fails != 0;
}
