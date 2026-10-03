/*
 * WS031 Linux-parity — PCH detection (soc/intel_pch.c).  See pch.h.
 */
#include "pch.h"
#include <kern/klog.h>
#include <drivers/pci.h>

/* soc/intel_pch.h device-id types (already masked with 0xff80). */
#define INTEL_PCH_DEVICE_ID_MASK        0xff80u
#define INTEL_PCH_IBX_DEVICE_ID_TYPE    0x3b00u
#define INTEL_PCH_CPT_DEVICE_ID_TYPE    0x1c00u
#define INTEL_PCH_PPT_DEVICE_ID_TYPE    0x1e00u
#define INTEL_PCH_LPT_DEVICE_ID_TYPE    0x8c00u
#define INTEL_PCH_LPT_LP_DEVICE_ID_TYPE 0x9c00u
#define INTEL_PCH_WPT_DEVICE_ID_TYPE    0x8c80u
#define INTEL_PCH_WPT_LP_DEVICE_ID_TYPE 0x9c80u
#define INTEL_PCH_SPT_DEVICE_ID_TYPE    0xA100u
#define INTEL_PCH_SPT_LP_DEVICE_ID_TYPE 0x9D00u
#define INTEL_PCH_KBP_DEVICE_ID_TYPE    0xA280u
#define INTEL_PCH_CNP_DEVICE_ID_TYPE    0xA300u
#define INTEL_PCH_CNP_LP_DEVICE_ID_TYPE 0x9D80u
#define INTEL_PCH_CMP_DEVICE_ID_TYPE    0x0280u
#define INTEL_PCH_CMP2_DEVICE_ID_TYPE   0x0680u
#define INTEL_PCH_CMP_V_DEVICE_ID_TYPE  0xA380u
#define INTEL_PCH_ICP_DEVICE_ID_TYPE    0x3480u
#define INTEL_PCH_ICP2_DEVICE_ID_TYPE   0x3880u
#define INTEL_PCH_MCC_DEVICE_ID_TYPE    0x4B00u
#define INTEL_PCH_TGP_DEVICE_ID_TYPE    0xA080u
#define INTEL_PCH_TGP2_DEVICE_ID_TYPE   0x4380u
#define INTEL_PCH_JSP_DEVICE_ID_TYPE    0x4D80u
#define INTEL_PCH_ADP_DEVICE_ID_TYPE    0x7A80u
#define INTEL_PCH_ADP2_DEVICE_ID_TYPE   0x5180u
#define INTEL_PCH_ADP3_DEVICE_ID_TYPE   0x7A00u
#define INTEL_PCH_ADP4_DEVICE_ID_TYPE   0x5480u
#define INTEL_PCH_P2X_DEVICE_ID_TYPE    0x7100u
#define INTEL_PCH_P3X_DEVICE_ID_TYPE    0x7000u
#define INTEL_PCH_QEMU_DEVICE_ID_TYPE   0x2900u   /* qemu q35 has 2918 */

#define PCI_VENDOR_ID_INTEL             0x8086u
#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4u
#define PCI_SUBDEVICE_ID_QEMU           0x1100u

/* PCI class 06h/01h (bridge / ISA), matched on class+subclass. */
#define PCI_CLASS_BRIDGE_ISA_24         0x060100u
#define PCI_CLASS_BRIDGE_ISA_MASK       0xffff00u

static const struct parity_pch_bridge_ops *g_bridge_test;

void
parity_pch_test_set_bridges(const struct parity_pch_bridge_ops *ops)
{
	g_bridge_test = ops;
}

int
parity_intel_pch_type(uint16_t id)
{
	switch (id) {
	case INTEL_PCH_IBX_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Ibex Peak PCH\n");
		return PARITY_PCH_IBX;
	case INTEL_PCH_CPT_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found CougarPoint PCH\n");
		return PARITY_PCH_CPT;
	case INTEL_PCH_PPT_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found PantherPoint PCH\n");
		return PARITY_PCH_CPT;          /* PPT is CPT compatible */
	case INTEL_PCH_LPT_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found LynxPoint PCH\n");
		return PARITY_PCH_LPT;
	case INTEL_PCH_LPT_LP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found LynxPoint LP PCH\n");
		return PARITY_PCH_LPT;
	case INTEL_PCH_WPT_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found WildcatPoint PCH\n");
		return PARITY_PCH_LPT;          /* WPT is LPT compatible */
	case INTEL_PCH_WPT_LP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found WildcatPoint LP PCH\n");
		return PARITY_PCH_LPT;          /* WPT is LPT compatible */
	case INTEL_PCH_SPT_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found SunrisePoint PCH\n");
		return PARITY_PCH_SPT;
	case INTEL_PCH_SPT_LP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found SunrisePoint LP PCH\n");
		return PARITY_PCH_SPT;
	case INTEL_PCH_KBP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Kaby Lake PCH (KBP)\n");
		return PARITY_PCH_SPT;
	case INTEL_PCH_CNP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Cannon Lake PCH (CNP)\n");
		return PARITY_PCH_CNP;
	case INTEL_PCH_CNP_LP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Cannon Lake LP PCH (CNP-LP)\n");
		return PARITY_PCH_CNP;
	case INTEL_PCH_CMP_DEVICE_ID_TYPE:
	case INTEL_PCH_CMP2_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Comet Lake PCH (CMP)\n");
		return PARITY_PCH_CNP;          /* CMP is CNP compatible */
	case INTEL_PCH_CMP_V_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Comet Lake V PCH (CMP-V)\n");
		return PARITY_PCH_SPT;          /* CMP-V is SPT compatible */
	case INTEL_PCH_ICP_DEVICE_ID_TYPE:
	case INTEL_PCH_ICP2_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Ice Lake PCH\n");
		return PARITY_PCH_ICP;
	case INTEL_PCH_MCC_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Mule Creek Canyon PCH\n");
		return PARITY_PCH_TGP;          /* MCC is TGP compatible */
	case INTEL_PCH_TGP_DEVICE_ID_TYPE:
	case INTEL_PCH_TGP2_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Tiger Lake LP PCH\n");
		return PARITY_PCH_TGP;
	case INTEL_PCH_JSP_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Jasper Lake PCH\n");
		return PARITY_PCH_ICP;          /* JSP is ICP compatible */
	case INTEL_PCH_ADP_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP2_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP3_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP4_DEVICE_ID_TYPE:
		kern_logf("i915: parity Found Alder Lake PCH\n");
		return PARITY_PCH_ADP;
	default:
		return PARITY_PCH_NONE;
	}
}

int
parity_intel_is_virt_pch(uint16_t id, uint16_t svendor, uint16_t sdevice)
{
	return (id == INTEL_PCH_P2X_DEVICE_ID_TYPE ||
		id == INTEL_PCH_P3X_DEVICE_ID_TYPE ||
		(id == INTEL_PCH_QEMU_DEVICE_ID_TYPE &&
		 svendor == PCI_SUBVENDOR_ID_REDHAT_QUMRANET &&
		 sdevice == PCI_SUBDEVICE_ID_QEMU)) ? 1 : 0;
}

/*
 * intel_virt_detect_pch(): in a virtualized passthrough environment the ISA
 * bridge may not be passed through, so guess the PCH from the graphics platform.
 * Only the ADL arm is reachable here (this port targets ADL-P); the others are
 * left out deliberately rather than guessed at.
 */
static void
intel_virt_detect_pch(int is_alderlake, uint16_t *pch_id, int *pch_type)
{
	uint16_t id = 0u;

	if (is_alderlake)
		id = INTEL_PCH_ADP_DEVICE_ID_TYPE;

	if (id != 0u)
		kern_logf("i915: parity Assuming PCH ID %04x\n", (unsigned)id);
	else
		kern_logf("i915: parity Assuming no PCH\n");

	*pch_type = parity_intel_pch_type(id);
	*pch_id = id;
}

/* The ISA-bridge walk: scripted in tests, a real PCI class scan otherwise. */
static int
next_isa_bridge(unsigned index, uint16_t *vendor, uint16_t *device,
	uint16_t *svid, uint16_t *sdid)
{
	static struct drv_pci_device *cursor;
	struct drv_pci_device *d;
	uint16_t v = 0u, p = 0u, sv = 0u, sd = 0u;

	if (g_bridge_test != 0 && g_bridge_test->next != 0)
		return g_bridge_test->next(g_bridge_test->ctx, index, vendor, device,
			svid, sdid);

	if (index == 0u)
		cursor = 0;
	d = drv_pci_find_class(PCI_CLASS_BRIDGE_ISA_24, PCI_CLASS_BRIDGE_ISA_MASK,
		cursor);
	if (d == 0)
		return 0;
	cursor = d;

	/*
	 * Read the identity from config space rather than the cached PCI header:
	 * on this host the cached accessors read back all-ones for the passthrough
	 * function, so the live path is the trustworthy one (see probe.c).
	 */
	(void)drv_pci_device_config_read16(d, 0x00u, &v);
	(void)drv_pci_device_config_read16(d, 0x02u, &p);
	(void)drv_pci_device_config_read16(d, 0x2cu, &sv);
	(void)drv_pci_device_config_read16(d, 0x2eu, &sd);
	*vendor = v; *device = p; *svid = sv; *sdid = sd;
	return 1;
}

void
parity_intel_detect_pch(struct parity_pch_state *p, int display_ver, int is_alderlake,
	int has_display, int run_as_guest)
{
	uint16_t vendor = 0u, device = 0u, svid = 0u, sdid = 0u;
	uint16_t id;
	int pch_type;
	int found_bridge = 0;
	unsigned i;

	p->type = PARITY_PCH_NONE;
	p->id = 0u;
	p->source = PARITY_PCH_SRC_NONE;
	p->bridges_scanned = 0u;
	p->bridge_device = 0u;
	p->bridge_svid = 0u;
	p->bridge_sdid = 0u;

	/*
	 * South display engine on the same PCI device -> fake PCH.  None of these
	 * apply to ADL-P (display ver 13), which really does pair with a PCH.
	 */
	if (display_ver >= 20) {
		p->type = PARITY_PCH_LNL;
		return;
	}

	/*
	 * Probe the ISA bridge rather than Dev31:Fun0 so that passthrough works:
	 * a VMM only has to expose an ISA bridge.  Scan ALL of them and take the
	 * first match -- some virtualized setups carry an irrelevant ISA bridge.
	 */
	for (i = 0u; ; i++) {
		if (!next_isa_bridge(i, &vendor, &device, &svid, &sdid))
			break;
		p->bridges_scanned++;
		found_bridge = 1;

		if (vendor != PCI_VENDOR_ID_INTEL)
			continue;

		id = (uint16_t)(device & INTEL_PCH_DEVICE_ID_MASK);

		pch_type = parity_intel_pch_type(id);
		if (pch_type != PARITY_PCH_NONE) {
			p->type = pch_type;
			p->id = id;
			p->source = PARITY_PCH_SRC_REAL;
			p->bridge_device = device;
			p->bridge_svid = svid;
			p->bridge_sdid = sdid;
			break;
		} else if (parity_intel_is_virt_pch(id, svid, sdid)) {
			intel_virt_detect_pch(is_alderlake, &p->id, &p->type);
			p->source = PARITY_PCH_SRC_VIRT;
			p->bridge_device = device;
			p->bridge_svid = svid;
			p->bridge_sdid = sdid;
			break;
		}
	}

	/* PCH_NOP = a PCH is present but there is no south display. */
	if (found_bridge && !has_display) {
		kern_logf("i915: parity Display disabled, reverting to NOP PCH\n");
		p->type = PARITY_PCH_NOP;
		p->id = 0u;
	} else if (!found_bridge) {
		if (run_as_guest && has_display) {
			intel_virt_detect_pch(is_alderlake, &p->id, &p->type);
			p->source = PARITY_PCH_SRC_NO_BRIDGE;
		} else {
			kern_logf("i915: parity No PCH found.\n");
		}
	}
}
