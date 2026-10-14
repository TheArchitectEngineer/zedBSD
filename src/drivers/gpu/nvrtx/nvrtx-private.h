/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Internal interface of the nvrtx driver (NVIDIA Turing and later GPUs).
 *
 * The driver is brought up one stage at a time (plan/ws037/nvrtx-design.md,
 * section 3).  This revision has the stages that only read: N0 finds the
 * GPU and reads its chip identity, N1 records where the firmware's picture
 * lives.  Nothing is written to the GPU and the function is left to the
 * firmware afterwards.
 */

#ifndef DRIVERS_GPU_NVRTX_PRIVATE_H
#define DRIVERS_GPU_NVRTX_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>

#include <drivers/pci/pci.h>

/* Marks a parameter a callback receives but does not need. */
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(parameter) ((void)(parameter))
#endif

/* The PCI vendor of every NVIDIA function. */
#define NVRTX_PCI_VENDOR		0x10deU

/* The PCI display class (VGA-compatible and 3D controllers), matched on its base class byte. */
#define NVRTX_PCI_CLASS_DISPLAY		0x030000U
#define NVRTX_PCI_CLASS_DISPLAY_MASK	0xff0000U

/*
 * The identity register at the start of the register window.
 *
 * Its bits 28:20 name the chip: bits 28:24 the architecture and bits 23:20
 * the implementation within it.  Bits 7:0 are the revision.
 */
#define NVRTX_REG_MC_IDENT_0		0x000000U
#define NVRTX_IDENT_CHIP_SHIFT		20U
#define NVRTX_IDENT_CHIP_MASK		0x1ffU
#define NVRTX_IDENT_ARCH_SHIFT		24U
#define NVRTX_IDENT_ARCH_MASK		0x1fU
#define NVRTX_IDENT_REVISION_MASK	0xffU

/*
 * The second identity register.
 *
 * It reads 0 when the GPU answers in little-endian order and 0x01000001
 * when it was switched to big-endian.  On Turing and later, bits 17:16 are
 * set on a virtual function of a partitioned GPU.
 */
#define NVRTX_REG_MC_IDENT_1		0x000004U
#define NVRTX_IDENT_1_BIG_ENDIAN	0x01000001U
#define NVRTX_IDENT_1_VIRTUAL_MASK	0x00030000U

/* The register window must reach past both identity registers; it is 16 MiB on Turing. */
#define NVRTX_REGISTER_WINDOW_MIN	0x1000U

/* How much of the register window N0 maps: the first page holds both identity registers. */
#define NVRTX_N0_MAP_BYTES		0x1000U

/* The PCI Express capability and the function-level-reset bit of its device capabilities. */
#define NVRTX_PCIE_CAPABILITY_ID	0x10U
#define NVRTX_PCIE_DEVICE_CAPABILITIES	0x04U
#define NVRTX_PCIE_DEVICE_CAP_FLR	(1U << 28)

/* The widest line a stage mark writes, so a mark never wraps on the console. */
#define NVRTX_STAGE_LINE_COLUMNS	79U

/* The memory windows N0 records: registers, VRAM, instance memory. */
#define NVRTX_WINDOW_COUNT		3U

/* The GPU generations, by the architecture field of the identity register. */
enum nvrtx_family {
	NVRTX_FAMILY_UNKNOWN = 0,
	NVRTX_FAMILY_TURING,
	NVRTX_FAMILY_AMPERE,
	NVRTX_FAMILY_HOPPER,
	NVRTX_FAMILY_ADA,
	NVRTX_FAMILY_BLACKWELL
};

/*
 * What the identity registers say about one GPU.
 *
 * Filled by nvrtx_identify() from the two register values alone, so the
 * decision a stage takes on it can be tested on the host.
 */
struct nvrtx_chip {
	uint32_t chipset;
	uint32_t revision;
	enum nvrtx_family family;
	const char *name;
	/* Nonzero when the GSP boot path of the first target (TU10x, firmware below tu102) applies. */
	int gsp_tu102;
};

/*
 * One memory window of the GPU, as its base address register describes it.
 *
 * The windows are counted in order of the memory BARs, because a 64-bit BAR
 * takes two register slots and moves the next window's slot number.
 */
struct nvrtx_window {
	int present;
	unsigned bar;
	uint64_t bus_address;
	uint64_t size;
	int is_64bit;
	int prefetchable;
};

/*
 * One NVIDIA GPU function while the driver looks at it.
 *
 * Lives on the attach's stack for the read-only stages: the attach takes the
 * register window, runs N0 and N1, and gives everything back before it
 * returns, so no state outlives the attach.
 */
struct nvrtx_device {
	struct drv_pci_device *pci;
	struct drv_pci_address address;
	uint16_t product;
	/* The command bits from before the attach, restored when it gives the function back. */
	struct drv_pci_enable_state enable_state;
	int enable_saved;
	struct nvrtx_window windows[NVRTX_WINDOW_COUNT];
	/* Set while the register window's BAR is claimed, so the release gives it back once. */
	int registers_claimed;
	struct drv_pci_mapping registers;
	struct nvrtx_chip chip;
	/* Nonzero when the function advertises a function-level reset. */
	int flr;
	/* Nonzero when N1 found the firmware's picture in this GPU's VRAM window: the GPU is the primary display. */
	int gop_on_gpu;
};

/*
 * Runs stage N0: finds the windows, maps the registers and reads the chip.
 */
int
nvrtx_n0_discover(
	struct nvrtx_device *device);

/*
 * Runs stage N1: records where the firmware's picture lies.
 */
void
nvrtx_n1_readout(
	struct nvrtx_device *device);

/*
 * Gives back everything N0 took, in the reverse order.
 */
void
nvrtx_n0_release(
	struct nvrtx_device *device);

/*
 * Decodes the chip identity from the two identity registers.
 *
 * Returns 0 for a Turing or later GPU the identity names, ENODEV for a GPU
 * the driver never drives (older generations, a virtual function, a
 * big-endian or unreadable identity), and ENOTSUP for a newer chip number
 * it does not know.
 */
int
nvrtx_identify(
	uint32_t ident0,
	uint32_t ident1,
	struct nvrtx_chip *chip);

/*
 * The device information table: 64 words naming the engines, their
 * registers and their reset bits in the PMC enable register.
 */
#define NVRTX_REG_DEVICE_INFO		0x022700U
#define NVRTX_DEVICE_INFO_WORDS		64U
#define NVRTX_DEVICE_INFO_KIND_MASK	0x3U
#define NVRTX_DEVICE_INFO_INVALID	0x0U
#define NVRTX_DEVICE_INFO_ENUM		0x2U
#define NVRTX_DEVICE_INFO_TYPE		0x3U
#define NVRTX_DEVICE_INFO_HAS_RESET	0x4U
#define NVRTX_DEVICE_INFO_RESET_SHIFT	9U
#define NVRTX_DEVICE_INFO_RESET_MASK	0x1fU
#define NVRTX_DEVICE_INFO_TYPE_SHIFT	2U
#define NVRTX_DEVICE_INFO_TYPE_MASK	0x1fffffffU
#define NVRTX_DEVICE_INFO_CHAINED	0x80000000U

/* The engine type of the secure processor (SEC2) that runs the booters. */
#define NVRTX_ENGINE_SEC2		0xdU

/*
 * Finds the reset bit of one engine in the device information table.
 */
int
nvrtx_device_info_reset(
	const uint32_t *words,
	uint32_t engine_type,
	uint32_t *reset_bit);

/*
 * Names a GPU generation for the stage marks.
 */
const char *
nvrtx_family_name(
	enum nvrtx_family family);

/*
 * Reports whether the boot asked the driver to stay out entirely (nvrtx.off=1).
 */
bool
nvrtx_stage_driver_off(void);

/*
 * Reports whether the boot asked for the GSP boot path (nvrtx.gsp=1).
 */
bool
nvrtx_stage_gsp_wanted(void);

/*
 * Reports whether the boot asked for the graphics golden context (nvrtx.gr=1).
 */
bool
nvrtx_stage_gr_wanted(void);

/*
 * Reports whether the boot asked for the first compute launch (nvrtx.compute=1).
 */
bool
nvrtx_stage_compute_wanted(void);

/*
 * Reports whether a stage may run; nvrtx.stop=<stage> stops before it.
 */
bool
nvrtx_stage_allowed(
	const char *stage);

/*
 * Writes one stage-mark line to the console and the kernel log.
 */
void
nvrtx_stage_mark(
	const char *format,
	...) __attribute__((format(printf, 1, 2)));

#endif
