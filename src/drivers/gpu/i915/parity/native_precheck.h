/*
 * WS031 Linux-parity -- N0: what the firmware left, recorded before the driver's first display write, and whether the
 * prepared start path applies.  zedBSD project code.  READ-ONLY: nothing here writes a register, the OpRegion or the
 * VT-d unit.  Registers of a power well that is off are "not readable" (never read as "disabled").
 *
 * Decision: PROCEED only when (a) no pipe is active (a pipe whose power domain is off is not_readable and counted inactive), (b) the VT-d unit that translates the
 * GPU is not translating and has no protected memory region enabled (zedBSD has no IOMMU driver), (c) the firmware
 * framebuffer's GGTT pages do not overlap the pages this driver writes.  Otherwise STOP, with the exact reason, before
 * any display write (N1 -- the takeover of a firmware display -- is not ported yet).
 */
#ifndef PARITY_NATIVE_PRECHECK_H
#define PARITY_NATIVE_PRECHECK_H

#include <stdint.h>
#include "opregion_vbt.h"

struct osdep_mmio;

enum parity_native_pipe_class {
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
	int cls;                        /* enum parity_native_pipe_class */
	uint32_t transconf, trans_ddi_func, pipesrc, plane_ctl, plane_surf, plane_stride, plane_size;
};

struct parity_native_report {
	int hypervisor;                 /* CPUID.1:ECX[31] */
	/* OpRegion / VBT */
	uint32_t asls;
	int opregion_mapped;
	struct parity_opregion_info op;
	int vbt_mapped, vbt_valid, vbt_matches_pin;
	uint32_t vbt_size;
	uint8_t vbt_sha256[32];
	/* the VT-d unit of the GPU (GFXVTBAR through the MCHBAR mirror) */
	uint64_t gfxvtbar;
	int vtd_enabled, vtd_readable;
	uint32_t vtd_ver, vtd_gsts, vtd_pmen;
	/* the firmware framebuffer */
	int fb_present;
	uint64_t fb_base, fb_size;
	int fb_in_aperture;
	uint32_t fb_ggtt_first, fb_ggtt_pages;
	/* display */
	struct parity_native_pipe pipe[4];
	unsigned active_pipes, unreadable_pipes;
	uint32_t pll_enable[2], ddi_buf_ctl_a, pp_status, pp_control, blc_ctl, blc_duty;
	uint32_t dpclka_cfgcr0;         /* ICL_DPCLKA_CFGCR0: the PHY -> PLL clock select (E-121) */
	/* the pages this driver will write in the GGTT */
	uint32_t driver_ggtt_first, ggtt_pages;
	int overlap;
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
};

struct parity_native_deps {
	struct osdep_mmio *mmio;
	uint32_t asls;
	uint64_t gmadr_base, gmadr_size;
	uint32_t ggtt_pages;            /* entries in the GGTT */
	uint32_t driver_ggtt_first;     /* the first GGTT page the driver writes (top window + display window) */
	/* the reference's readout gate: is the pipe's / transcoder's power domain on (no write) */
	int (*pipe_powered)(void *ctx, unsigned pipe);
	void *ctx;
	const uint8_t *vbt_pin;         /* the explicit blob's pinned sha256 (may be 0) */
	/* what the VBT parser actually consumed (intel_bios_init ran before N0) */
	int parser_src;
	uint32_t parser_size;
	const uint8_t *parser_sha256;   /* 0 when not known */
};

/*
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

/* fills *r; returns r->proceed */
int parity_native_precheck(const struct parity_native_deps *d, struct parity_native_report *r);
void parity_native_log(const struct parity_native_report *r);
/* the last logged record once more (end of the run: it stays on the screen) */
void parity_native_log_again(void);
/* the decision alone, from the recorded facts (pure; host-tested) */
void parity_native_decide(struct parity_native_report *r);

#endif /* PARITY_NATIVE_PRECHECK_H */
