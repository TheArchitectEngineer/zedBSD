/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The check of what the firmware left on a Gen9 display (see
 * gen9-firmware.h).
 *
 * For every pipe whose power is up, the first plane's enable, surface,
 * stride and size are read and logged, and a picture that reaches into
 * the GGTT pages the driver is about to write is refused.  The GPU's
 * VT-d unit is read through the MCHBAR mirror: a unit that translates or
 * protects memory would fault the GT's DMA, and zedBSD has no IOMMU
 * driver.  Either way the display is marked as not driven.
 */

#include "internal.h"
#include "gen9-firmware.h"

#include "../mmio.h"
#include "../intel/display-regs-gen9.h"

#include <hal/hal.h>
#include <kern/klog.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Where the GPU's VT-d unit is described: the GFXVTBAR register of the
 * host bridge, seen through the MCHBAR mirror of the GPU's MMIO.  Bit 0
 * enables the unit; the 4 KiB-aligned rest is the unit's address.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/intel_mchbar_regs.h
 * (MCHBAR_MIRROR_BASE_SNB), and the Intel Core processor datasheets, Vol. 2
 * (Host Bridge MCHBAR, GFXVTBAR at offset 5400h).
 */
#define I915_GEN9_FIRMWARE_MCHBAR_MIRROR	0x140000U
#define I915_GEN9_FIRMWARE_GFXVTBAR		(I915_GEN9_FIRMWARE_MCHBAR_MIRROR + 0x5400U)
#define I915_GEN9_FIRMWARE_GFXVTBAR_ENABLE	0x1ULL
#define I915_GEN9_FIRMWARE_GFXVTBAR_BASE	(~0xfffULL)

/*
 * The registers of a VT-d unit the check reads: the version (non-zero
 * on a real unit), the global status (bit 31: translation enabled) and
 * the protected memory enable (bit 0: enable, bit 31: status), within the
 * unit's 4 KiB register page.
 *
 * Values referenced: Intel Virtualization Technology for Directed I/O
 * Architecture Specification (VER_REG, GSTS_REG with TES, PMEN_REG with
 * EPM and PRS).
 */
#define I915_GEN9_FIRMWARE_VTD_VER		0x00U
#define I915_GEN9_FIRMWARE_VTD_GSTS		0x1cU
#define I915_GEN9_FIRMWARE_VTD_GSTS_TES		0x80000000U
#define I915_GEN9_FIRMWARE_VTD_PMEN		0x64U
#define I915_GEN9_FIRMWARE_VTD_PMEN_ACTIVE	0x80000001U
#define I915_GEN9_FIRMWARE_VTD_WINDOW		0x1000U

/* The bytes of one GGTT page. */
#define I915_GEN9_FIRMWARE_PAGE_BYTES		4096U

/*
 * The fields of the first plane's registers the check decodes: the
 * surface's 4 KiB-aligned GGTT offset, the linear stride in 64-byte
 * units, and the height less one in the upper half of the size.
 */
#define I915_GEN9_FIRMWARE_SURFACE_MASK		0xfffff000U
#define I915_GEN9_FIRMWARE_STRIDE_MASK		0x7ffU
#define I915_GEN9_FIRMWARE_STRIDE_UNIT		64U
#define I915_GEN9_FIRMWARE_HEIGHT_SHIFT		16U
#define I915_GEN9_FIRMWARE_HEIGHT_MASK		0xfffU

/*
 * What the check read from the GPU's VT-d unit.
 *
 * One instance lives on the stack of the check; nothing keeps it after.
 */
struct i915_gen9_firmware_vtd {
	/* The GFXVTBAR register as read: the enable bit and the unit's address. */
	uint64_t gfxvtbar;

	/* Nonzero when GFXVTBAR enables the unit. */
	int enabled;

	/* Nonzero when the unit's registers answered with a real version. */
	int readable;

	/* The version, the global status and the protected memory enable, as read. */
	uint32_t version;
	uint32_t status;
	uint32_t protect;
};

static int i915_gen9_firmware_hypervisor(void);
static void i915_gen9_firmware_read_vtd(struct i915_mmio *mmio, struct i915_gen9_firmware_vtd *vtd);
static int i915_gen9_firmware_vtd_blocks(int hypervisor, const struct i915_gen9_firmware_vtd *vtd);
static int i915_gen9_firmware_plane(struct i915_mmio *mmio, unsigned pipe, uint64_t driver_start);

/*
 * Checks what the firmware scans out on a Gen9 device and leaves the display unused.
 *
 * Nothing is written to the hardware.  The display is marked absent in
 * every case.  Returns 0, EBUSY when a picture reaches into the driver's
 * GGTT pages, or EIO when an active VT-d unit would translate the GT's
 * DMA.
 */
int
drv_i915_gen9_firmware_check(
	struct i915_display *display,
	struct i915_mmio *mmio,
	unsigned driver_ggtt_first)
{
	struct i915_gen9_firmware_vtd vtd;
	uint64_t driver_start;
	uint32_t power;
	uint32_t edp_transconf;
	uint32_t powered_bit;
	unsigned pipe;
	int hypervisor;
	int overlap;
	int vtd_active;
	int reaches;
	const char *where_running;

	/* Reads which display power wells the firmware left up, and where the driver's GGTT pages begin. */
	power = drv_i915_raw_read32(mmio, I915_SKL_PWR_WELL_CTL2);
	driver_start = (uint64_t)driver_ggtt_first * I915_GEN9_FIRMWARE_PAGE_BYTES;
	kern_logf("i915: gen9 firmware display: PWR_WELL_CTL2=0x%08x driver GGTT from 0x%llx\n",
		  power,
		  (unsigned long long)driver_start);

	/* Logs the eDP transcoder, which a laptop's firmware drives its panel with and which needs power well 1. */
	if ((power & I915_SKL_PW_1_STATE) != 0U) {
		edp_transconf = drv_i915_raw_read32(mmio, I915_SKL_TRANSCONF_EDP);
		kern_logf("i915: gen9 firmware display: TRANSCONF_EDP=0x%08x\n", edp_transconf);
	}

	/* Asks whether a hypervisor runs the machine, and reads the GPU's VT-d unit. */
	hypervisor = i915_gen9_firmware_hypervisor();
	i915_gen9_firmware_read_vtd(mmio, &vtd);

	/* Names where the driver runs, for the log. */
	where_running = "native";
	if (hypervisor != 0)
		where_running = "guest";

	/* Logs what the translation unit looks like. */
	kern_logf("i915: gen9 firmware display: GFXVTBAR=0x%llx enabled=%d %s readable=%d GSTS=0x%08x PMEN=0x%08x\n",
		  (unsigned long long)vtd.gfxvtbar,
		  vtd.enabled,
		  where_running,
		  vtd.readable,
		  vtd.status,
		  vtd.protect);

	/* Decides whether the unit would get in the way of the GT's DMA. */
	vtd_active = i915_gen9_firmware_vtd_blocks(hypervisor, &vtd);

	/* Looks at the first plane of every powered pipe. */
	overlap = 0;
	for (pipe = 0U; pipe < I915_SKL_PIPES; pipe++) {
		/* Pipe A is in power well 1; pipes B and C are in power well 2 as well. */
		powered_bit = I915_SKL_PW_1_STATE;
		if (pipe != 0U)
			powered_bit = I915_SKL_PW_2_STATE;

		/* An unpowered pipe's registers do not answer and are not read. */
		if ((power & powered_bit) == 0U)
			continue;

		/* An unpowered power well 1 leaves every pipe unpowered. */
		if ((power & I915_SKL_PW_1_STATE) == 0U)
			continue;

		/* Reads the plane and remembers a picture that reaches the driver's pages. */
		reaches = i915_gen9_firmware_plane(mmio, pipe, driver_start);
		if (reaches != 0)
			overlap = 1;
	}

	/* Records that the display controller is not driven: the GPU node has no display. */
	display->absent = 1;
	kern_logf("i915: gen9 firmware display: the firmware's picture is kept; the GPU node has no display\n");

	/* Refuses a GT start whose DMA the VT-d unit would translate or block. */
	if (vtd_active != 0) {
		kern_logf("i915: gen9 firmware display: the GPU's VT-d unit translates or protects memory and zedBSD has no IOMMU driver: the GT is not started\n");
		return EIO;
	}

	/* Refuses a GT start that would overwrite the firmware's picture. */
	if (overlap != 0)
		return EBUSY;

	/* Succeeded: the picture is clear of the driver's GGTT pages and nothing translates the GT's DMA. */
	return 0;
}

/* Reports the hypervisor bit of the processor (CPUID leaf 1, ECX bit 31). */
static int
i915_gen9_firmware_hypervisor(void)
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;

	/* Asks CPUID leaf 1. */
	eax = 1U;
	__asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));

	/* Only ECX carries the bit; the other two outputs are dropped. */
	(void)ebx;
	(void)edx;

	/* Succeeded: reports whether the bit is set. */
	return (int)((ecx >> 31) & 1U);
}

/* Reads the GPU's VT-d unit through the MCHBAR mirror, without writing it. */
static void
i915_gen9_firmware_read_vtd(
	struct i915_mmio *mmio,
	struct i915_gen9_firmware_vtd *vtd)
{
	uint32_t low;
	uint32_t high;
	uint64_t base;
	void *unit;
	int error;

	/* Starts from a unit that is neither enabled nor readable. */
	vtd->gfxvtbar = 0U;
	vtd->enabled = 0;
	vtd->readable = 0;
	vtd->version = 0U;
	vtd->status = 0U;
	vtd->protect = 0U;

	/* Reads GFXVTBAR, the low half first. */
	low = drv_i915_read32(mmio, I915_GEN9_FIRMWARE_GFXVTBAR);
	high = drv_i915_read32(mmio, I915_GEN9_FIRMWARE_GFXVTBAR + 4U);
	vtd->gfxvtbar = (uint64_t)low | (uint64_t)high << 32;

	/* A disabled unit is not read. */
	if ((vtd->gfxvtbar & I915_GEN9_FIRMWARE_GFXVTBAR_ENABLE) == 0U)
		return;
	vtd->enabled = 1;

	/* A unit at address 0 is not read either. */
	base = vtd->gfxvtbar & I915_GEN9_FIRMWARE_GFXVTBAR_BASE;
	if (base == 0U)
		return;

	/* Maps the unit's register page for reading. */
	unit = NULL;
	error = hal_space_map_device((hal_physaddr_t)base, I915_GEN9_FIRMWARE_VTD_WINDOW, HAL_SPACE_READ, &unit);
	if (error != HAL_OK)
		return;
	if (unit == NULL)
		return;

	/* Reads the version, the global status and the protected memory enable, and drops the mapping. */
	vtd->version = *(const volatile uint32_t *)((const volatile uint8_t *)unit + I915_GEN9_FIRMWARE_VTD_VER);
	vtd->status = *(const volatile uint32_t *)((const volatile uint8_t *)unit + I915_GEN9_FIRMWARE_VTD_GSTS);
	vtd->protect = *(const volatile uint32_t *)((const volatile uint8_t *)unit + I915_GEN9_FIRMWARE_VTD_PMEN);
	(void)hal_space_unmap_device(unit, I915_GEN9_FIRMWARE_VTD_WINDOW);

	/*
	 * A real unit answers with a version that is neither 0 nor all ones;
	 * otherwise nothing decodes at that address (a guest sees the host's
	 * address, for one) and the other two words are not register values.
	 */
	if (vtd->version == 0U)
		return;
	if (vtd->version == 0xffffffffU)
		return;
	vtd->readable = 1;
}

/* Tells whether the VT-d unit read would translate or block the GT's DMA. */
static int
i915_gen9_firmware_vtd_blocks(
	int hypervisor,
	const struct i915_gen9_firmware_vtd *vtd)
{
	/* A guest's view of the unit says nothing about the host's translation. */
	if (hypervisor != 0)
		return 0;

	/* A disabled unit translates nothing. */
	if (vtd->enabled == 0)
		return 0;

	/* An enabled unit that cannot be read may translate, so it is taken as active. */
	if (vtd->readable == 0)
		return 1;

	/* A unit with translation enabled is active. */
	if ((vtd->status & I915_GEN9_FIRMWARE_VTD_GSTS_TES) != 0U)
		return 1;

	/* A unit with protected memory enabled or in effect is active. */
	if ((vtd->protect & I915_GEN9_FIRMWARE_VTD_PMEN_ACTIVE) != 0U)
		return 1;

	/* Succeeded: the unit leaves the GT's DMA alone. */
	return 0;
}

/*
 * Logs the first plane of a powered pipe and tells whether the picture it
 * scans out reaches the driver's GGTT pages.
 */
static int
i915_gen9_firmware_plane(
	struct i915_mmio *mmio,
	unsigned pipe,
	uint64_t driver_start)
{
	uint64_t surface_start;
	uint64_t surface_bytes;
	uint64_t rows;
	uint32_t control;
	uint32_t stride;
	uint32_t size;
	uint32_t surface;
	uint32_t offset;

	/* Reads the plane's enable, layout and surface. */
	offset = pipe * I915_SKL_PIPE_STRIDE;
	control = drv_i915_raw_read32(mmio, I915_SKL_PLANE_CTL_1_A + offset);
	stride = drv_i915_raw_read32(mmio, I915_SKL_PLANE_STRIDE_1_A + offset);
	size = drv_i915_raw_read32(mmio, I915_SKL_PLANE_SIZE_1_A + offset);
	surface = drv_i915_raw_read32(mmio, I915_SKL_PLANE_SURF_1_A + offset);
	kern_logf("i915: gen9 firmware display: pipe %u PLANE_CTL=0x%08x STRIDE=0x%08x SIZE=0x%08x SURF=0x%08x\n",
		  pipe,
		  control,
		  stride,
		  size,
		  surface);

	/* A disabled plane scans nothing out. */
	if ((control & I915_SKL_PLANE_CTL_ENABLE) == 0U)
		return 0;

	/* Works out the bytes the picture covers from its surface, its stride and its height. */
	surface_start = (uint64_t)(surface & I915_GEN9_FIRMWARE_SURFACE_MASK);
	rows = (uint64_t)(((size >> I915_GEN9_FIRMWARE_HEIGHT_SHIFT) & I915_GEN9_FIRMWARE_HEIGHT_MASK) + 1U);
	surface_bytes = (uint64_t)(stride & I915_GEN9_FIRMWARE_STRIDE_MASK) * I915_GEN9_FIRMWARE_STRIDE_UNIT * rows;

	/* A picture that ends inside the driver's pages would be overwritten. */
	if (surface_start + surface_bytes > driver_start) {
		kern_logf("i915: gen9 firmware display: pipe %u scans out 0x%llx..0x%llx, inside the driver's GGTT pages\n",
			  pipe,
			  (unsigned long long)surface_start,
			  (unsigned long long)(surface_start + surface_bytes));
		return 1;
	}

	/* Succeeded: the picture stays clear of the driver's pages. */
	return 0;
}
