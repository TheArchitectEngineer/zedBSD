/*
 * WS031 Linux-parity — real MMIO / forcewake backend.
 *
 * Binds the osdep MMIO contract (osdep/mmio.h) to zedBSD's kern_mmio_* over the
 * mapped register BAR, and to the Gen9+ forcewake request/ack registers.  Only
 * the OS boundary lives here; the forcewake refcount/ACK LOGIC is in osdep.
 */
#include "../internal.h"
#include <kern/device-io.h>
#include "osdep/mmio.h"
#include "osdep/pci.h"
#include "backend.h"

/* Gen9+ forcewake control registers (always-on; accessed raw, not domain-gated). */
#define PARITY_FW_KERNEL          0x00000001u
#define PARITY_FW_REQ_RENDER      0x0000a278u
#define PARITY_FW_ACK_RENDER      0x00000d84u
#define PARITY_FW_REQ_GT          0x0000a188u
#define PARITY_FW_ACK_GT          0x00130044u
/* FORCEWAKE_MEDIA_VDBOX_GEN11(n) = 0xa540 + n*4, ack = 0xd50 + n*4. */
#define PARITY_FW_REQ_VDBOX0      0x0000a540u
#define PARITY_FW_ACK_VDBOX0      0x00000d50u
#define PARITY_FW_REQ_VDBOX2      0x0000a548u
#define PARITY_FW_ACK_VDBOX2      0x00000d58u
/* FORCEWAKE_MEDIA_VEBOX_GEN11(n) = 0xa560 + n*4, ack = 0xd70 + n*4. */
#define PARITY_FW_REQ_VEBOX0      0x0000a560u
#define PARITY_FW_ACK_VEBOX0      0x00000d70u

/*
 * The Gen12 register-to-domain map, generated from the reference
 * __gen12_fw_ranges (gt_fw_ranges.inc).  Only registers reached through the
 * domain-checked osdep_mmio_* paths consult it; the forcewake control
 * registers themselves are reached through the backend directly.
 */
const struct osdep_mmio_range parity_mmio_ranges[] = {
#include "gt_fw_ranges.inc"
};
const unsigned parity_mmio_range_count =
	sizeof(parity_mmio_ranges) / sizeof(parity_mmio_ranges[0]);

static uint32_t
b_raw_read32(void *priv, uint32_t off)
{
	struct parity_mmio_priv *p = priv;

	if ((unsigned long)off > p->size - 4ul)
		return 0xffffffffu;
	return kern_mmio_read32(p->base + off);
}

static void
b_raw_write32(void *priv, uint32_t off, uint32_t v)
{
	struct parity_mmio_priv *p = priv;

	if ((unsigned long)off > p->size - 4ul)
		return;
	kern_mmio_write32(p->base + off, v);
}

static uint32_t
fw_req_reg(int domain)
{
	switch (domain) {
	case OSDEP_FW_GT:            return PARITY_FW_REQ_GT;
	case OSDEP_FW_MEDIA_VDBOX0:  return PARITY_FW_REQ_VDBOX0;
	case OSDEP_FW_MEDIA_VDBOX2:  return PARITY_FW_REQ_VDBOX2;
	case OSDEP_FW_MEDIA_VEBOX0:  return PARITY_FW_REQ_VEBOX0;
	default:                     return PARITY_FW_REQ_RENDER;
	}
}

static uint32_t
fw_ack_reg(int domain)
{
	switch (domain) {
	case OSDEP_FW_GT:            return PARITY_FW_ACK_GT;
	case OSDEP_FW_MEDIA_VDBOX0:  return PARITY_FW_ACK_VDBOX0;
	case OSDEP_FW_MEDIA_VDBOX2:  return PARITY_FW_ACK_VDBOX2;
	case OSDEP_FW_MEDIA_VEBOX0:  return PARITY_FW_ACK_VEBOX0;
	default:                     return PARITY_FW_ACK_RENDER;
	}
}

static void
b_fw_request(void *priv, int domain, int wake)
{
	struct parity_mmio_priv *p = priv;
	uint32_t val = (PARITY_FW_KERNEL << 16) | (wake ? PARITY_FW_KERNEL : 0u);

	/* Masked write: only the KERNEL bit is affected. */
	kern_mmio_write32(p->base + fw_req_reg(domain), val);
}

static int
b_fw_ack(void *priv, int domain)
{
	struct parity_mmio_priv *p = priv;
	uint32_t ack = kern_mmio_read32(p->base + fw_ack_reg(domain));

	return (ack & PARITY_FW_KERNEL) != 0u;
}

static const struct osdep_mmio_backend parity_mmio_backend_def = {
	"zedbsd-mmio",
	b_raw_read32,
	b_raw_write32,
	b_fw_request,
	b_fw_ack,
};

const struct osdep_mmio_backend *
parity_mmio_backend(void)
{
	return &parity_mmio_backend_def;
}

/* --- runtime-PM stub backend (init_early during P0..P2 does not resume/suspend) --- */
static int  rpm_resume(void *priv)  { (void)priv; return 0; }
static void rpm_suspend(void *priv) { (void)priv; }

static const struct osdep_rpm_backend parity_rpm_backend_def = {
	"zedbsd-rpm", rpm_resume, rpm_suspend,
};

const struct osdep_rpm_backend *
parity_rpm_backend(void)
{
	return &parity_rpm_backend_def;
}

/* --- PCI-core probe runtime-PM backend: resume performs a real D0 transition --- */
static int
pci_probe_pm_resume(void *priv)
{
	/*
	 * pm_runtime resume: bring the PCI device to D0.  A real PMCSR write when
	 * the device exposes a PM capability; a legitimate no-op when it has none
	 * (not an empty stub -- the actual set_power_state runs either way).
	 */
	return osdep_pci_set_power_state((struct osdep_pci *)priv, OSDEP_PCI_D0);
}

static void
pci_probe_pm_suspend(void *priv)
{
	(void)osdep_pci_set_power_state((struct osdep_pci *)priv, OSDEP_PCI_D3HOT);
}

static const struct osdep_rpm_backend parity_pci_probe_pm_backend_def = {
	"pci-probe-pm", pci_probe_pm_resume, pci_probe_pm_suspend,
};

const struct osdep_rpm_backend *
parity_pci_probe_pm_backend(void)
{
	return &parity_pci_probe_pm_backend_def;
}
