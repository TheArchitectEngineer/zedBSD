/*
 * WS031 Linux-parity — intel_bios_init (VBT acquisition + parse), see bios.c.
 *
 * Structure-faithful port of intel_bios_init(): initialise the VBT device/block
 * lists, apply init_vbt_defaults(), then resolve the VBT from (1) the OpRegion
 * (carried from P2), else (2) the PCI expansion ROM read for real via config
 * 0x30 + a device mapping.  A found+validated VBT is parsed (BDB blocks, general
 * features/definitions); a genuine absence takes init_vbt_missing_defaults()
 * (default child devices for the non-TC DDI ports).  DGFX SPI flash is NOT read
 * on ADL-P (not IS_DGFX).  This never fabricates a VBT and never fails the probe
 * (intel_bios_init is void): it reports which real source was used.
 */
#ifndef PARITY_BIOS_H
#define PARITY_BIOS_H

#include <stdint.h>
#include <stddef.h>

struct osdep_pci;
struct osdep_trace;

enum parity_vbt_source {
	PARITY_VBT_SRC_NONE = 0,     /* no VBT found -> missing defaults */
	PARITY_VBT_SRC_OPREGION,     /* OpRegion mailbox #4 (from P2) */
	PARITY_VBT_SRC_PCI_ROM,      /* PCI expansion ROM ($VBT) */
};

/* One VBT child device (child_device_config subset used for default gen). */
struct parity_vbt_child {
	unsigned port;               /* source DDI port (diagnostic) */
	uint8_t  dvo_port;           /* DVO_PORT_* */
	uint32_t device_type;        /* DEVICE_TYPE_* bitmask */
};

/* VBT-derived display state (drm_i915_private.display.vbt subset). */
struct parity_vbt_state {
	int has_display;                 /* HAS_DISPLAY snapshot */
	uint16_t version;                /* display.vbt.version */
	int vbt_found;                   /* a real VBT was located + validated */
	int source;                      /* enum parity_vbt_source */
	int missing_defaults_used;       /* init_vbt_missing_defaults() ran */
	unsigned num_bdb_blocks;         /* bdb_blocks list length */
	unsigned num_display_devices;    /* display_devices list length */
	struct parity_vbt_child display_devices[8];
};

/*
 * intel_bios_init(): opregion_has_vbt reflects the SAME-boot P2 OpRegion state
 * (1 only when P2 actually holds a usable OpRegion VBT; 0 for a genuine absence,
 * e.g. ASLS==0).  `pci` is used to read the PCI ROM when the OpRegion has none.
 * Returns 0 (the reference is void); `vbt` is filled with what was actually done.
 */
int parity_intel_bios_init(struct parity_vbt_state *vbt, struct osdep_pci *pci,
	int opregion_has_vbt, struct osdep_trace *trace);

/* Exposed for GPU-free tests: validate a VBT buffer / parse an in-memory VBT. */
int parity_bios_is_valid_vbt(const void *buf, size_t size);
int parity_bios_process_vbt(struct parity_vbt_state *vbt, const void *buf, size_t size);
void parity_bios_init_vbt_missing_defaults(struct parity_vbt_state *vbt);

#endif /* PARITY_BIOS_H */
