/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One GPU the nvrtx driver keeps, and the parts of the GSP boot path that
 * touch the kernel: the register window, DMA memory, firmware files and the
 * worker thread.
 */

#ifndef DRIVERS_GPU_NVRTX_GPU_H
#define DRIVERS_GPU_NVRTX_GPU_H

#include <stddef.h>
#include <stdint.h>

#include <drivers/generic/dma.h>
#include <drivers/pci/pci.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"
#include "drivers/gpu/nvrtx/nvrtx-private.h"

/* The firmware directory the GSP files are read from, for every TU10x chip. */
#define NVRTX_FIRMWARE_DIRECTORY	"/lib/firmware/nvidia/tu102/"

/* The registers P0 reads: the VRAM size and the display's VGA workspace. */
#define NVRTX_REG_VRAM_SIZE		0x100ce0U
#define NVRTX_REG_VGA_WORKSPACE		0x625f04U

/* The largest VBIOS the driver reads. */
#define NVRTX_VBIOS_MAX_BYTES		0x100000U

/* The chunk the firmware image is held in: whole pages, under the bus's segment limit. */
#define NVRTX_IMAGE_CHUNK_BYTES		0x100000U

/* The largest GSP image the three-level table describes with one middle-level page (512 x 512 pages). */
#define NVRTX_IMAGE_MAX_BYTES		0x40000000U

/* How far the GSP boot got; each later state includes the earlier ones. */
enum nvrtx_gsp_state {
	NVRTX_GSP_NONE = 0,
	NVRTX_GSP_PREPARED,
	NVRTX_GSP_FAILED
};

/*
 * One coherent DMA buffer the GSP reads, with the device it came from.
 */
struct nvrtx_buffer {
	struct drv_dma_buffer dma;
	int allocated;
};

/*
 * A firmware file the boot reads, small enough to hold in kernel memory.
 */
struct nvrtx_blob {
	uint8_t *data;
	size_t size;
};

/*
 * The memory the GSP boot prepares in P0 and keeps while the GSP may read it.
 *
 * Every buffer stays allocated until the GSP is stopped (P7) or, when the
 * boot never started the GSP, until the worker gives the preparation back.
 */
struct nvrtx_boot_memory {
	/* The GSP firmware image (.fwimage) in 1 MiB chunks, and its three-level table. */
	struct nvrtx_pages image;
	struct nvrtx_buffer *image_chunks;
	struct nvrtx_pages table;
	struct nvrtx_buffer table_buffer;
	uint8_t *table_cpu;
	uint64_t table_bus;
	size_t image_size;

	/* The image's signature, the RISC-V bootloader's payload and the placement record. */
	struct nvrtx_buffer signature;
	size_t signature_size;
	struct nvrtx_buffer bootloader;
	struct nvrtx_buffer record;

	/* The start page of the GSP's operating system, its three logs and the start arguments. */
	struct nvrtx_buffer libos;
	struct nvrtx_buffer logs[3];
	struct nvrtx_buffer rmargs;

	/* The shared queue memory and the channel's staging buffer. */
	struct nvrtx_buffer queues;
	struct nvrtx_pages queue_pages;
	uint8_t *queue_cpu;
	uint64_t queue_bus;
	uint8_t *staging;

	/* The two FWSEC programs, each with its command written. */
	struct nvrtx_buffer fwsec_frts;
	struct nvrtx_buffer fwsec_sb;
};

/*
 * One GPU the driver keeps after N1, when the boot asked for the GSP path.
 *
 * Created by the attach and owned by the PCI binding; the worker thread runs
 * the GSP boot and is the only writer of the state while it runs.  The
 * register window stays mapped for as long as the binding lasts.
 */
struct nvrtx_gpu {
	struct nvrtx_device identity;
	struct drv_pci_mapping registers;
	int registers_claimed;
	struct drv_dma_device *dma;

	/* The firmware files read in P0. */
	struct nvrtx_blob booter_load;
	struct nvrtx_blob booter_unload;
	struct nvrtx_blob bootloader_file;
	struct nvrtx_blob falcon_bootloader;
	struct nvrtx_booter load;
	struct nvrtx_booter unload;
	struct nvrtx_bootloader bootloader_desc;

	/* The VBIOS read in P0 and its FWSEC program. */
	uint8_t *vbios_data;
	size_t vbios_size;
	struct nvrtx_vbios vbios;
	struct nvrtx_fwsec fwsec;

	/* The placement in the VRAM and the memory the GSP reads. */
	struct nvrtx_wpr_layout layout;
	struct nvrtx_boot_memory memory;
	struct nvrtx_channel channel;

	/* Set by the worker; read by detach only after the worker has ended. */
	enum nvrtx_gsp_state state;
	volatile int worker_running;
};

uint32_t nvrtx_rd32(const struct nvrtx_gpu *gpu, uint32_t offset);
void nvrtx_wr32(const struct nvrtx_gpu *gpu, uint32_t offset, uint32_t value);

int nvrtx_buffer_alloc(struct nvrtx_gpu *gpu, size_t size, struct nvrtx_buffer *buffer);
void nvrtx_buffer_free(struct nvrtx_gpu *gpu, struct nvrtx_buffer *buffer);

int nvrtx_file_load(const char *name, size_t limit, struct nvrtx_blob *blob);
void nvrtx_file_unload(struct nvrtx_blob *blob);
int nvrtx_file_load_image(struct nvrtx_gpu *gpu, const char *name);

int nvrtx_rom_read(struct nvrtx_gpu *gpu);

int nvrtx_gsp_start(struct nvrtx_gpu *gpu);
void nvrtx_gsp_release(struct nvrtx_gpu *gpu);

#endif
