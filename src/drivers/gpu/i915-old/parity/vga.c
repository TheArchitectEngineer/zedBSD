/*
 * WS031 Linux-parity — intel_vga_register (see vga.h).
 *
 * intel_vga_register() registers this display device with the VGA arbiter,
 * passing intel_gmch_vga_set_decode as the decode callback.  The arbiter returns
 * -ENODEV for a device that is not the PCI VGA display class (a secondary
 * controller that does not take part in arbitration); that is tolerated, every
 * other error is returned.  The decode callback calls intel_gmch_vga_set_state,
 * which read-modify-writes the GMCH VGA-disable bit in GMCH_CTRL of the HOST
 * BRIDGE (00:00.0) as a 16-bit config word -- not the GPU function.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <drivers/pci.h>
#include <kern/device-io.h>
#include <errno.h>
#include "parity.h"
#include "osdep/trace.h"
#include "vga.h"
#include "wait.h"
#include "osdep/mmio.h"

/* GMCH control register (host bridge config) + VGA-disable bit. */
#define PARITY_SNB_GMCH_CTRL           0x50u   /* DISPLAY_VER >= 6 */
#define PARITY_INTEL_GMCH_CTRL         0x52u   /* older */
#define PARITY_INTEL_GMCH_VGA_DISABLE  (1u << 1)

/* PCI VGA display class (base 0x03, subclass 0x00). */
#define PCI_CLASS_VGA_MASK      0xffff00u
#define PCI_CLASS_VGA           0x030000u

int
parity_intel_gmch_vga_set_state(struct parity_vga_client *c, int enable_decode)
{
	unsigned reg = (c->display_ver >= 6u) ? PARITY_SNB_GMCH_CTRL : PARITY_INTEL_GMCH_CTRL;
	uint16_t gmch_ctrl = 0;

	if (c->gmch == 0)
		return -ENODEV;   /* no host bridge: GMCH_CTRL is not reachable */

	if (drv_pci_device_config_read16(c->gmch, reg, &gmch_ctrl) != 0) {
		kern_logf("i915: parity vga: failed to read GMCH control word\n");
		return -EIO;
	}

	/* Already in the requested state? (VGA_DISABLE set == decode disabled) */
	if ((!!(gmch_ctrl & PARITY_INTEL_GMCH_VGA_DISABLE)) == (!enable_decode))
		return 0;

	if (enable_decode)
		gmch_ctrl &= (uint16_t)~PARITY_INTEL_GMCH_VGA_DISABLE;
	else
		gmch_ctrl |= PARITY_INTEL_GMCH_VGA_DISABLE;

	if (drv_pci_device_config_write16(c->gmch, reg, gmch_ctrl) != 0) {
		kern_logf("i915: parity vga: failed to write GMCH control word\n");
		return -EIO;
	}
	return 0;
}

unsigned
parity_intel_gmch_vga_set_decode(struct parity_vga_client *c, int enable_decode)
{
	(void)parity_intel_gmch_vga_set_state(c, enable_decode);

	if (enable_decode)
		return PARITY_VGA_RSRC_LEGACY_IO | PARITY_VGA_RSRC_LEGACY_MEM |
		       PARITY_VGA_RSRC_NORMAL_IO | PARITY_VGA_RSRC_NORMAL_MEM;
	return PARITY_VGA_RSRC_NORMAL_IO | PARITY_VGA_RSRC_NORMAL_MEM;
}

/*
 * vga_client_register(): register the decode callback against the arbiter's
 * entry for this device.  The arbiter only tracks PCI VGA-class devices; a
 * non-VGA (secondary) controller is not present in its list, so registration
 * returns -ENODEV.  Here we reproduce that by checking the device's PCI class.
 */
static int
parity_vga_client_register(struct parity_vga_client *c)
{
	uint32_t class_code = drv_pci_device_class(c->gpu);

	if ((class_code & PCI_CLASS_VGA_MASK) != PCI_CLASS_VGA)
		return -ENODEV;   /* !PCI_DISPLAY_CLASS_VGA: not an arbiter client */

	c->registered = 1;
	return 0;
}

int
parity_intel_vga_register(struct parity_vga_client *c, struct drv_pci_device *gpu,
	unsigned display_ver, struct osdep_trace *trace)
{
	struct drv_pci_address bridge = { 0u, 0u, 0u, 0u };   /* 00:00.0 GMCH */
	int ret;

	c->gpu = gpu;
	c->display_ver = display_ver;
	c->registered = 0;

	/*
	 * i915->gmch.pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0)).
	 * The decode callback needs it to reach GMCH_CTRL on the host bridge.
	 */
	c->gmch = drv_pci_find_device(&bridge);
	if (c->gmch == 0)
		osdep_trace_emit(trace, PARITY_STAGE_P3, OSDEP_TR_NOTE,
			"intel_vga_register:no_gmch_bridge", 0u, 0u);

	ret = parity_vga_client_register(c);
	if (ret != 0 && ret != -ENODEV)
		return ret;   /* a real arbiter error, not the tolerated -ENODEV */

	osdep_trace_emit(trace, PARITY_STAGE_P3,
		ret == 0 ? OSDEP_TR_ACQUIRE : OSDEP_TR_NOTE,
		ret == 0 ? "intel_vga_register" : "intel_vga_register:enodev_secondary",
		(uint64_t)(unsigned)ret, 0u);
	kern_logf("i915: parity P3 intel_vga_register: client=%s gmch_bridge=%s ret=%d\n",
		c->registered ? "registered" : "not-vga(-ENODEV)",
		c->gmch ? "found" : "absent", ret);
	return 0;
}

void
parity_intel_vga_unregister(struct parity_vga_client *c)
{
	/* vga_client_unregister(): drop the decode callback for this device. */
	c->registered = 0;
	c->gpu = 0;
	c->gmch = 0;
}

#define VGA_RSRC_LEGACY_IO 1
/* Standard VGA sequencer ports (video/vga.h). */
#define VGA_SEQ_I 0x3C4u            /* Sequencer index */
#define VGA_SEQ_D 0x3C5u            /* Sequencer data */
#define VGA_SR01_SCREEN_OFF 0x20u   /* SR01 bit 5: screen off */
#define CPU_VGACNTRL 0x41000u       /* DISPLAY_VER >= 5 */
#define VGA_DISP_DISABLE (1u << 31)

#define VGA_MIS_R 0x3CCu   /* Misc Output read */
#define VGA_MIS_W 0x3C2u   /* Misc Output write */

/* Injectable legacy VGA I/O accessor (NULL in production -> arbiter + port I/O). */
static const struct parity_vga_io_ops *g_vga_io;

void
parity_vga_io_test_set(const struct parity_vga_io_ops *ops)
{
	g_vga_io = ops;
}

static int
vga_get_legacy_io(void)
{
	/* Production: we own the legacy VGA IO on this device, so the get succeeds. */
	return g_vga_io != 0 ? g_vga_io->get(g_vga_io->ctx, VGA_RSRC_LEGACY_IO) : 1;
}
static unsigned char
vga_in8(unsigned short port)
{
	return g_vga_io != 0 ? g_vga_io->in8(g_vga_io->ctx, port) : kern_io_in8(port);
}
static void
vga_out8(unsigned short port, unsigned char val)
{
	if (g_vga_io != 0) g_vga_io->out8(g_vga_io->ctx, port, val);
	else kern_io_out8(port, val);
}
static void
vga_put_legacy_io(void)
{
	if (g_vga_io != 0) g_vga_io->put(g_vga_io->ctx, VGA_RSRC_LEGACY_IO);
}

void
parity_intel_vga_reset_io_mem(struct parity_vga_client *c)
{
	/*
	 * intel_vga_reset_io_mem(): own the LEGACY_IO arbiter resource, touch the
	 * VGA MSR (read MIS_R, write it back to MIS_W) so vgacon stays sane, then
	 * release the resource.  I/O ownership is INDEPENDENT of decode-client
	 * registration; if the resource cannot be acquired we do no I/O and no put.
	 */
	(void)c;
	if (!vga_get_legacy_io())
		return;
	vga_out8(VGA_MIS_W, vga_in8(VGA_MIS_R));
	vga_put_legacy_io();
}

/*
 * intel_vga_disable(): "Disable the VGA plane that we never use".  The
 * WaEnableVGAAccessThroughIOPort sequence turns the screen off through the
 * legacy sequencer FIRST, waits 300us, and only then sets VGA_DISP_DISABLE in
 * CPU_VGACNTRL -- doing it the other way round loses the IO port access.
 */
int
parity_intel_vga_disable(struct parity_vga_client *c, struct osdep_mmio *m)
{
	unsigned char sr1;

	(void)c;

	if (osdep_mmio_read32(m, CPU_VGACNTRL) & VGA_DISP_DISABLE)
		return 1;   /* already disabled; the reference returns here */

	/* WaEnableVGAAccessThroughIOPort */
	if (!vga_get_legacy_io())
		return -EIO;
	vga_out8(VGA_SEQ_I, 0x01u);
	sr1 = vga_in8(VGA_SEQ_D);
	vga_out8(VGA_SEQ_D, (unsigned char)(sr1 | VGA_SR01_SCREEN_OFF));
	vga_put_legacy_io();
	(void)parity_udelay(300u);

	osdep_mmio_write32(m, CPU_VGACNTRL, VGA_DISP_DISABLE);
	osdep_mmio_posting_read32(m, CPU_VGACNTRL);
	return 0;
}
