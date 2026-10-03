/*
 * WS031 Linux-parity — intel_vga_register (VGA arbiter client), see vga.c.
 *
 * Ports intel_vga_register() / intel_vga_unregister() and the decode callback
 * intel_gmch_vga_set_decode() -> intel_gmch_vga_set_state().  The arbiter client
 * is registered against the display device (VGA class); a genuine
 * non-VGA/secondary controller yields -ENODEV (tolerated), any other error is
 * returned.  The decode callback drives GMCH_CTRL on the HOST BRIDGE
 * (i915->gmch.pdev = 00:00.0), NOT the GPU function, with a 16-bit masked
 * read-modify-write, exactly as the reference does.
 */
#ifndef PARITY_VGA_H
#define PARITY_VGA_H

#include <stdint.h>

struct drv_pci_device;
struct osdep_trace;

/* video/vga.h resource flags returned by the decode callback. */
#define PARITY_VGA_RSRC_LEGACY_IO   0x01u
#define PARITY_VGA_RSRC_LEGACY_MEM  0x02u
#define PARITY_VGA_RSRC_NORMAL_IO   0x04u
#define PARITY_VGA_RSRC_NORMAL_MEM  0x08u

/* VGA arbiter client state (a faithful subset of the registration). */
struct parity_vga_client {
	struct drv_pci_device *gpu;   /* display device (arbiter client) */
	struct drv_pci_device *gmch;  /* host bridge 00:00.0 (GMCH_CTRL owner) */
	unsigned display_ver;         /* selects SNB_GMCH_CTRL vs INTEL_GMCH_CTRL */
	int registered;               /* callback installed (0 after unregister) */
};

/*
 * intel_vga_register(): vga_client_register(pdev, intel_gmch_vga_set_decode).
 * Returns 0 on success OR on the genuine -ENODEV (secondary controller) branch;
 * any other arbiter error is propagated.
 */
int parity_intel_vga_register(struct parity_vga_client *c, struct drv_pci_device *gpu,
	unsigned display_ver, struct osdep_trace *trace);

/* intel_vga_unregister(): vga_client_unregister(pdev). */
void parity_intel_vga_unregister(struct parity_vga_client *c);

/* Exposed for tests: the decode callback and the GMCH_CTRL state change. */
unsigned parity_intel_gmch_vga_set_decode(struct parity_vga_client *c, int enable_decode);
int parity_intel_gmch_vga_set_state(struct parity_vga_client *c, int enable_decode);

/*
 * intel_vga_disable(): turn off the VGA plane we never use.  Lives here, as in
 * the reference (intel_vga.c), because it needs the legacy-IO accessors.
 * Returns 1 if VGA_DISP_DISABLE was already set (nothing done), 0 if the
 * disable sequence ran, or -EIO if the legacy-IO resource was unavailable.
 */
struct osdep_mmio;
int parity_intel_vga_disable(struct parity_vga_client *c, struct osdep_mmio *m);

/* intel_vga_reset_io_mem(): touch the VGA MSR after a power-well enable. */
void parity_intel_vga_reset_io_mem(struct parity_vga_client *c);

/*
 * Legacy VGA I/O accessor (intel_vga_reset_io_mem's get/in/out/put).  In
 * production these are the real arbiter + port I/O; a GPU-free test injects a
 * recorder so the arbiter get -> MIS_R read -> MIS_W write -> put sequence is
 * checked without touching real VGA ports.  get() returns 1 on success, 0 if
 * the legacy-IO resource could not be acquired (then no I/O and no put).
 */
struct parity_vga_io_ops {
	int (*get)(void *ctx, int rsrc);
	unsigned char (*in8)(void *ctx, unsigned short port);
	void (*out8)(void *ctx, unsigned short port, unsigned char val);
	void (*put)(void *ctx, int rsrc);
	void *ctx;
};
void parity_vga_io_test_set(const struct parity_vga_io_ops *ops);


#endif /* PARITY_VGA_H */
