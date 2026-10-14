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

/* The registers P1a reads: GFW's devinit is done when bit 0 of the first is set and the second's low byte is 0xFF. */
#define NVRTX_REG_DEVINIT_LEVEL		0x118128U
#define NVRTX_REG_DEVINIT_PROGRESS	0x118234U
#define NVRTX_DEVINIT_COMPLETED		0xffU

/* The registers P0 reads: the VRAM size and the display's VGA workspace. */
#define NVRTX_REG_VRAM_SIZE		0x100ce0U
#define NVRTX_REG_VGA_WORKSPACE		0x625f04U

/* The largest VBIOS the driver reads. */
#define NVRTX_VBIOS_MAX_BYTES		0x100000U

/* The chunk the firmware image is held in: whole pages, under the bus's segment limit. */
#define NVRTX_IMAGE_CHUNK_BYTES		0x100000U

/* The largest GSP image the three-level table describes with one middle-level page (512 x 512 pages). */
#define NVRTX_IMAGE_MAX_BYTES		0x40000000U

/* The handles the driver gives its objects of the resource manager. */
#define NVRTX_HANDLE_CLIENT		0xc0de0001U
#define NVRTX_HANDLE_DEVICE		0xc0de0080U
#define NVRTX_HANDLE_SUBDEVICE		0xc0de2080U
#define NVRTX_HANDLE_SPACE		0xc0de90f1U
#define NVRTX_HANDLE_CHANNEL		0xc0dec46fU
#define NVRTX_HANDLE_COPY		0xc0dec5b5U

/* How long a call to the resource manager waits for its reply. */
#define NVRTX_CALL_TIMEOUT_US		5000000U

/* The two falcons the boot drives: the GSP's own and the secure processor's. */
#define NVRTX_FALCON_GSP_BASE		0x110000U
#define NVRTX_FALCON_SEC2_BASE		0x840000U

/*
 * One falcon: its register base and, for the secure processor, its bit in
 * the PMC enable register (from the device information table).
 */
struct nvrtx_falcon {
	uint32_t base;
	uint32_t pmc_mask;
};

/* How far the GSP boot got; each later state includes the earlier ones. */
enum nvrtx_gsp_state {
	NVRTX_GSP_NONE = 0,
	NVRTX_GSP_PREPARED,
	NVRTX_GSP_REGION_SET,
	NVRTX_GSP_RUNNING,
	NVRTX_GSP_FAILED,
	/* A falcon did not halt or the region could not be torn down: memory is kept until power-off. */
	NVRTX_GSP_FAILED_HOLD
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
 * The first address space and channel (stages P4 to P6).
 *
 * The VRAM block holds the page tables, the instance block and USERD; the
 * system-memory buffers are the ring, the push buffer, the semaphore page and
 * the method buffer.  The flags say which resource-manager objects exist, so
 * the stop frees only those.
 */
struct nvrtx_engine_state {
	uint64_t vram_block;
	struct nvrtx_buffer ring;
	struct nvrtx_buffer push;
	struct nvrtx_buffer semaphore;
	struct nvrtx_buffer methods;
	uint32_t methods_size;
	struct nvrtx_engine_pick pick;
	int space_made;
	int root_set;
	int channel_made;
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
	struct nvrtx_falcon_bl falcon_bl;

	/* The VBIOS read in P0 and its FWSEC program. */
	uint8_t *vbios_data;
	size_t vbios_size;
	struct nvrtx_vbios vbios;
	struct nvrtx_fwsec fwsec;

	/* The two falcons. */
	struct nvrtx_falcon gsp_falcon;
	struct nvrtx_falcon sec2_falcon;

	/* The placement in the VRAM and the memory the GSP reads. */
	struct nvrtx_wpr_layout layout;
	struct nvrtx_boot_memory memory;
	struct nvrtx_channel channel;
	/* The last call's sequence number, and the buffer each received message is copied into. */
	uint32_t call_sequence;
	uint8_t *reply;
	uint8_t *call;

	/* The first address space and channel. */
	struct nvrtx_engine_state engine;

	/* The static information P3 read, and the resource manager's own client and subdevice. */
	uint8_t *static_info;
	uint32_t internal_client;
	uint32_t internal_subdevice;

	/* Set by the worker; read by detach only after the worker has ended. */
	enum nvrtx_gsp_state state;
	/* Set just before FWSEC-FRTS runs: from then on the end of the boot tears the region down or reports it. */
	int region_written;
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

int nvrtx_poll(struct nvrtx_gpu *gpu, uint32_t offset, uint32_t mask, uint32_t value, uint32_t timeout_us);
uint64_t nvrtx_now_us(void);
int nvrtx_falcon_reset(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
int nvrtx_falcon_reset_engine(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
void nvrtx_falcon_load_physical(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
void nvrtx_falcon_dma_system(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, uint32_t context);
uint32_t nvrtx_falcon_code_limit(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
int nvrtx_falcon_imem(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, const uint8_t *code, uint32_t length, uint32_t address, uint32_t tag, int secure);
int nvrtx_falcon_dmem(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, const uint8_t *data, uint32_t length, uint32_t address);
int nvrtx_falcon_boot(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, uint32_t boot_address, uint32_t *mailbox0, uint32_t *mailbox1);

int nvrtx_fwsec_run(struct nvrtx_gpu *gpu, const struct nvrtx_buffer *program);
int nvrtx_booter_run(struct nvrtx_gpu *gpu, const struct nvrtx_booter *booter, uint32_t mailbox0, uint32_t mailbox1);
int nvrtx_region_tear_down(struct nvrtx_gpu *gpu);
void nvrtx_region_bounds(struct nvrtx_gpu *gpu, uint32_t *low, uint32_t *high);
uint32_t nvrtx_frts_result(struct nvrtx_gpu *gpu);

int nvrtx_rpc_send(struct nvrtx_gpu *gpu, uint32_t function, const uint8_t *payload, size_t length, int with_sequence, uint32_t *sequence);
int nvrtx_rpc_wait(struct nvrtx_gpu *gpu, uint32_t function, uint32_t sequence, uint32_t timeout_us, size_t *length);

int nvrtx_rm_allocate(struct nvrtx_gpu *gpu, uint32_t parent, uint32_t handle, uint32_t class_number, const uint8_t *parameters, size_t length);
int nvrtx_rm_control(struct nvrtx_gpu *gpu, uint32_t client, uint32_t object, uint32_t command, const uint8_t *parameters, size_t length, const uint8_t **reply, size_t *reply_length);
int nvrtx_rm_free(struct nvrtx_gpu *gpu, uint32_t object);

int nvrtx_space_start(struct nvrtx_gpu *gpu);
int nvrtx_channel_start(struct nvrtx_gpu *gpu);
void nvrtx_channel_stop(struct nvrtx_gpu *gpu);
void nvrtx_channel_release(struct nvrtx_gpu *gpu);

int nvrtx_gsp_start(struct nvrtx_gpu *gpu);
void nvrtx_gsp_release(struct nvrtx_gpu *gpu);

#endif
