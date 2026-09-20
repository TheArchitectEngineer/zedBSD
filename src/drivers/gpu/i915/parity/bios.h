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
#include "vbt/parity_vbt.h"

struct osdep_pci;
struct osdep_trace;

enum parity_vbt_source {
	PARITY_VBT_SRC_NONE = 0,     /* no VBT found -> missing defaults */
	PARITY_VBT_SRC_OPREGION,     /* OpRegion mailbox #4 (from P2) */
	PARITY_VBT_SRC_PCI_ROM,      /* PCI expansion ROM ($VBT) */
	PARITY_VBT_SRC_EXPLICIT_BLOB, /* a named, hash-pinned blob the build asked for -- NOT an OpRegion */
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

	/* the reference parser's result (vbt/intel_bios_port.c); device-owned, released by driver_remove */
	struct parity_vbt parsed;
	int parsed_live;

	/* explicit blob bookkeeping (only meaningful when PARITY_VBT_EXPLICIT) */
	const char *blob_name;
	unsigned blob_size;
	uint8_t blob_sha256[32];
	int blob_requested, blob_found, blob_hash_ok, blob_subsys_ok, blob_valid;
	uint16_t subsys_vendor, subsys_device;
};

/*
 * The explicit VBT is used only when the build asks for it.  It supplies the target's
 * configuration data; it does not make an OpRegion exist (ASLS stays what the firmware left).
 */
/* the display test configuration (the one-shot real AUX acquisition) asks for the explicit VBT */
#ifndef PARITY_AUX_TEST
#define PARITY_AUX_TEST 0
#endif
#ifndef PARITY_LCDB_TEST
#define PARITY_LCDB_TEST 0            /* LCD-B: one picture on the panel (implies the explicit VBT) */
#endif
#ifndef PARITY_LCDR_TEST
#define PARITY_LCDR_TEST 0            /* LCD reuse: three cycles + IRQ + brightness (implies the explicit VBT) */
#endif
#ifndef PARITY_LCDG_TEST
#define PARITY_LCDG_TEST 0            /* LCD-G: a GPU-drawn full-HD image shown from the same backing */
#endif
#ifndef PARITY_LCDC_TEST
#define PARITY_LCDC_TEST 0            /* LCD-C: synchronous flips between two buffers */
#endif
#ifndef PARITY_N0_FORCE_STOP
#define PARITY_N0_FORCE_STOP 0        /* VM test of the N0 STOP path (the early teardown); never in a native image */
#endif
#ifndef PARITY_OPREGION_FW_TEST
#define PARITY_OPREGION_FW_TEST 0     /* E-123: the OpRegion service on the REAL OpRegion (native; writes the mailboxes) */
#endif
#ifndef PARITY_LCDO_TEST
#define PARITY_LCDO_TEST 0            /* LCD-O: synthetic ASLE brightness on the real LCD (shadow OpRegion) */
#endif
#ifndef PARITY_HDMI_HPD_TEST
#define PARITY_HDMI_HPD_TEST 0        /* E-123: HPD-TEST window, the HDMI cable plugged / unplugged (no GPU submission) */
#endif
#ifndef PARITY_DUAL_SHARE_TEST
#define PARITY_DUAL_SHARE_TEST 0      /* E-123: one buffer, both screens (the external one sees part of it) */
#endif
#ifndef PARITY_DUAL_TEST
#define PARITY_DUAL_TEST 0            /* E-123: the panel and the external HDMI display at once */
#endif
#ifndef PARITY_HDMI_B_TEST
#define PARITY_HDMI_B_TEST 0          /* E-123: one picture on the external HDMI display, then the stop path */
#endif
#ifndef PARITY_HDMI_EDID_TEST
#define PARITY_HDMI_EDID_TEST 0       /* E-123: the connected HDMI sink's first detection + EDID over GMBUS */
#endif
#ifndef PARITY_LCDD_TEST
#define PARITY_LCDD_TEST 0            /* LCD-D: GPU back-buffer redraw + synchronous flip */
#endif
#ifndef PARITY_N1_TEST
#define PARITY_N1_TEST 0      /* E-124: the display the firmware left running (readout, takeover, re-light) */
#endif
#ifndef PARITY_VBT_EXPLICIT
#define PARITY_VBT_EXPLICIT (PARITY_N1_TEST || PARITY_DUAL_SHARE_TEST || PARITY_DUAL_TEST || PARITY_HDMI_B_TEST || PARITY_HDMI_EDID_TEST || PARITY_HDMI_HPD_TEST || PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST || PARITY_LCDO_TEST)
#endif
/*
 * The explicit VBT inputs, one row per machine this build carries.  A row is used only when the
 * file is found, its sha256 matches the pin AND the PCI subsystem id is that machine -- the same
 * rule as the single pin it replaces (E-107), now for more than one target (E-126).
 */
#define PARITY_VBT_EXPLICIT_NAME "zedbsd/vbt/dell-latitude-5330-1028-0b02.vbt"   /* the first target */
#define PARITY_VBT_EXPLICIT_SUBSYS_VENDOR 0x1028u
#define PARITY_VBT_EXPLICIT_SUBSYS_DEVICE 0x0b02u

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
/* intel_bios_driver_remove(): releases the parser's lists and arena. */
void parity_intel_bios_driver_remove(struct parity_vbt_state *vbt);
/* `explicit_blob` != 0 asks for the named blob (the probe passes PARITY_VBT_EXPLICIT). */
int parity_intel_bios_init_ex(struct parity_vbt_state *vbt, struct osdep_pci *pci,
	int opregion_has_vbt, int explicit_blob, struct osdep_trace *trace);
void parity_sha256(const void *data, size_t len, uint8_t out[32]);
const uint8_t *parity_vbt_explicit_pin(void);
/* E-122: the OpRegion VBT (P2's read-only copy) offered to intel_bios_init, after the explicit blob */
void parity_bios_set_opregion_vbt(const void *buf, size_t size);

#endif /* PARITY_BIOS_H */
