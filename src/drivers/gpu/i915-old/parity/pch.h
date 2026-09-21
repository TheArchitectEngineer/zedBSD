/*
 * WS031 Linux-parity — PCH detection (soc/intel_pch.c).
 *
 * P4 needs the PCH type: gen11_display_irq_reset() resets SDE and
 * gen8_de_irq_postinstall() runs icp_irq_postinstall() only when
 * INTEL_PCH_TYPE(i915) >= PCH_ICP.  ADL-P pairs with PCH_ADP (8) >= PCH_ICP (6).
 *
 * In a passthrough guest the real PCH is usually NOT passed through, and the
 * reference handles that explicitly: QEMU's q35 ISA bridge (0x2918, masked to
 * INTEL_PCH_QEMU_DEVICE_ID_TYPE 0x2900) with a Red Hat / QEMU subsystem id is
 * recognised as virtual, and intel_virt_detect_pch() then GUESSES the PCH from
 * the graphics platform -- ADL-S/ADL-P -> INTEL_PCH_ADP_DEVICE_ID_TYPE.  So the
 * expected outcome here is PCH_ADP via the virtual path, not via a real PCH.
 */
#ifndef PARITY_PCH_H
#define PARITY_PCH_H

#include <stdint.h>

/* enum intel_pch, values preserved (the enum is ordered by south-display
 * compatibility and is compared with >=). */
enum parity_pch {
	PARITY_PCH_NOP = -1,      /* PCH without south display */
	PARITY_PCH_NONE = 0,      /* No PCH present */
	PARITY_PCH_IBX,           /* 1 */
	PARITY_PCH_CPT,           /* 2 */
	PARITY_PCH_LPT,           /* 3 */
	PARITY_PCH_SPT,           /* 4 */
	PARITY_PCH_CNP,           /* 5 */
	PARITY_PCH_ICP,           /* 6 */
	PARITY_PCH_TGP,           /* 7 */
	PARITY_PCH_ADP,           /* 8 -- Alder Lake */
	/* Fake PCHs, functionality handled on the same PCI dev. */
	PARITY_PCH_DG1 = 1024,
	PARITY_PCH_DG2,
	PARITY_PCH_MTL,
	PARITY_PCH_LNL
};

/* How the type was reached (diagnostic; the reference only logs this). */
enum parity_pch_source {
	PARITY_PCH_SRC_NONE = 0,
	PARITY_PCH_SRC_REAL,      /* a real ISA bridge matched the id table */
	PARITY_PCH_SRC_VIRT,      /* an emulated bridge -> platform guess */
	PARITY_PCH_SRC_NO_BRIDGE  /* no ISA bridge at all -> guest guess */
};

struct parity_pch_state {
	int type;                 /* enum parity_pch */
	uint16_t id;              /* masked PCH device id */
	int source;               /* enum parity_pch_source */
	unsigned bridges_scanned;
	uint16_t bridge_device;   /* raw device id of the bridge that decided it */
	uint16_t bridge_svid, bridge_sdid;
};

struct drv_pci_device;

/* Map a masked PCH device id to a PCH type, or PARITY_PCH_NONE if unknown. */
int parity_intel_pch_type(uint16_t id);

/* intel_is_virt_pch(): an emulated south bridge we must guess behind. */
int parity_intel_is_virt_pch(uint16_t id, uint16_t svendor, uint16_t sdevice);

/*
 * intel_detect_pch().  `is_alderlake` selects the platform guess used by
 * intel_virt_detect_pch(); `has_display` drives the PCH_NOP case.  `run_as_guest`
 * is the reference's i915_run_as_guest() (no ISA bridge found at all).
 */
void parity_intel_detect_pch(struct parity_pch_state *p, int display_ver,
	int is_alderlake, int has_display, int run_as_guest);

/*
 * Test seam: the ISA-bridge walk.  Production uses the real PCI scan; the
 * GPU-free tests install a scripted list so the virtual/real/absent paths are
 * all reachable without hardware.  Returns 1 and fills the ids while entries
 * remain, 0 when the walk is finished.
 */
struct parity_pch_bridge_ops {
	int (*next)(void *ctx, unsigned index, uint16_t *vendor, uint16_t *device,
		uint16_t *svid, uint16_t *sdid);
	void *ctx;
};
void parity_pch_test_set_bridges(const struct parity_pch_bridge_ops *ops);

#endif /* PARITY_PCH_H */
