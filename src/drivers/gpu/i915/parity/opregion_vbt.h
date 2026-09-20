/*
 * WS031 Linux-parity -- where the ACPI OpRegion keeps the VBT (intel_opregion_setup(), v6.8.12).  zedBSD project code;
 * the offsets and the decision order are the reference's (struct opregion_header / struct opregion_asle,
 * OPREGION_*_OFFSET, MBOX_*).  Pure: it reads a copy of the 8 KiB OpRegion and says where the VBT is; the caller maps
 * and validates it (intel_bios_is_valid_vbt).  intel_load_vbt_firmware() and the DMI quirk list are not consulted here.
 */
#ifndef PARITY_OPREGION_VBT_H
#define PARITY_OPREGION_VBT_H

#include <stdint.h>
#include <stddef.h>

#define PARITY_OPREGION_SIZE        0x2000u
#define PARITY_OPREGION_ASLE_OFFSET 0x300u
#define PARITY_OPREGION_VBT_OFFSET  0x400u
#define PARITY_OPREGION_ASLE_EXT    0x1c00u

enum parity_opregion_vbt_src { PARITY_OPVBT_NONE = 0, PARITY_OPVBT_RVDA, PARITY_OPVBT_MAILBOX4 };

struct parity_opregion_info {
	int signature_ok;
	uint32_t size_kib, mboxes;
	uint8_t major, minor, revision;
	uint64_t rvda;                  /* as stored */
	uint32_t rvds;
	int rvda_relative;              /* 2.1+: an offset from the OpRegion base */
	int rvda_inside;                /* 2.1+ and rvda < OPREGION_SIZE: the reference WARNs */
	/* the candidate the reference tries first: RVDA (physical address), else mailbox #4 (offset in the OpRegion) */
	int src;
	uint64_t vbt_phys;              /* RVDA */
	uint32_t vbt_offset, vbt_max;   /* mailbox #4: offset and the size the reference allows */
	/* runtime mailboxes as the firmware (or a previous driver) left them -- OBSERVED, never written by this driver */
	uint32_t acpi_drdy, acpi_csts, acpi_cevt, acpi_chpd, acpi_clid;   /* struct opregion_acpi at 0x100 */
	uint32_t asle_ardy, asle_aslc, asle_tche;                         /* struct opregion_asle at 0x300 */
};

/* 0, or -1 when the buffer is too short / the signature is wrong (nothing else is filled then) */
int parity_opregion_locate_vbt(const uint8_t *op, size_t len, uint64_t asls, struct parity_opregion_info *out);

#endif /* PARITY_OPREGION_VBT_H */
