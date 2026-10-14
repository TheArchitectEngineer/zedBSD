/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GSP boot of the nvrtx driver.
 *
 * Stage P0 prepares everything the later stages need before anything is
 * written that cannot be taken back (plan/ws037/nvrtx-design.md, section 2):
 * the firmware files, the DMA memory the GSP reads, the placement of the
 * firmware in the VRAM, and the two FWSEC programs from the VBIOS.  The only
 * writes are the VBIOS windows, which are put back at once.  A failure in P0
 * leaves the GPU as the firmware left it.
 *
 * The stages that write (P1 and later) are not in this revision; the worker
 * stops after P0 and gives the preparation back.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The firmware files, below the firmware directory. */
#define GSP_FILE_IMAGE			"gsp/gsp-570.144.bin"
#define GSP_FILE_BOOTLOADER		"gsp/bootloader-570.144.bin"
#define GSP_FILE_BOOTER_LOAD		"gsp/booter_load-570.144.bin"
#define GSP_FILE_BOOTER_UNLOAD		"gsp/booter_unload-570.144.bin"
#define GSP_FILE_FALCON_BOOTLOADER	"acr/bl.bin"

/* The largest small file read whole. */
#define GSP_SMALL_FILE_MAX		0x40000U

/* The page of the queue memory that lists its pages, counted with the rest. */
#define GSP_QUEUE_PTE_COUNT		(NVRTX_QUEUE_SHARED_BYTES / NVRTX_GSP_PAGE_SIZE)

static int gsp_prepare(struct nvrtx_gpu *gpu);
static int gsp_load_files(struct nvrtx_gpu *gpu);
static int gsp_prepare_image(struct nvrtx_gpu *gpu);
static int gsp_prepare_placement(struct nvrtx_gpu *gpu);
static int gsp_prepare_start(struct nvrtx_gpu *gpu);
static int gsp_prepare_fwsec(struct nvrtx_gpu *gpu);

/*
 * Runs the GSP boot as far as this revision goes.
 *
 * Called on the worker thread.  Returns 0 when the boot stopped where it
 * was asked or built to, or the error of the stage that failed; either way
 * the GPU is as the firmware left it.
 */
int
nvrtx_gsp_start(
	struct nvrtx_gpu *gpu)
{
	bool allowed;
	int error;

	/* Stops before P0 when the boot asks. */
	allowed = nvrtx_stage_allowed("P0");
	if (!allowed) {
		nvrtx_stage_mark("P0 skipped (nvrtx.stop=P0)");
		return 0;
	}

	/* Prepares everything P1 and later need. */
	nvrtx_stage_mark("P0 begin");
	error = gsp_prepare(gpu);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail (%d); the GPU is untouched", error);
		gpu->state = NVRTX_GSP_FAILED;
		nvrtx_gsp_release(gpu);
		return error;
	}

	gpu->state = NVRTX_GSP_PREPARED;
	nvrtx_stage_mark("P0 ok vram %lluM wpr2 %llx+%lluM heap %lluM",
	    (unsigned long long)(gpu->layout.vram_size >> 20),
	    (unsigned long long)gpu->layout.region,
	    (unsigned long long)(gpu->layout.region_size >> 20),
	    (unsigned long long)(gpu->layout.heap_size >> 20));

	/* Says why the boot goes no further, and gives the preparation back. */
	allowed = nvrtx_stage_allowed("P1a");
	if (!allowed) {
		nvrtx_stage_mark("P1a skipped (nvrtx.stop=P1a)");
	} else {
		nvrtx_stage_mark("P1a is not in this kernel; stopping after P0");
	}

	nvrtx_gsp_release(gpu);

	/* Succeeded: the boot stopped after P0 with nothing written. */
	return 0;
}

/*
 * Gives back everything P0 prepared.
 *
 * Only for a GPU whose GSP was never started: once the GSP may read this
 * memory it is kept until the GSP is stopped.
 */
void
nvrtx_gsp_release(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_boot_memory *memory;
	size_t chunk;
	unsigned index;

	memory = &gpu->memory;

	/* Frees the image's chunks and their bookkeeping. */
	if (memory->image_chunks != NULL) {
		for (chunk = 0U; chunk < memory->image.chunk_count; chunk++)
			nvrtx_buffer_free(gpu, &memory->image_chunks[chunk]);
		kern_free(memory->image_chunks);
	}

	if (memory->image.chunk_cpu != NULL)
		kern_free(memory->image.chunk_cpu);
	if (memory->image.chunk_bus != NULL)
		kern_free(memory->image.chunk_bus);

	/* Frees the single buffers. */
	nvrtx_buffer_free(gpu, &memory->table_buffer);
	nvrtx_buffer_free(gpu, &memory->signature);
	nvrtx_buffer_free(gpu, &memory->bootloader);
	nvrtx_buffer_free(gpu, &memory->record);
	nvrtx_buffer_free(gpu, &memory->libos);
	for (index = 0U; index < 3U; index++)
		nvrtx_buffer_free(gpu, &memory->logs[index]);
	nvrtx_buffer_free(gpu, &memory->rmargs);
	nvrtx_buffer_free(gpu, &memory->queues);
	nvrtx_buffer_free(gpu, &memory->fwsec_frts);
	nvrtx_buffer_free(gpu, &memory->fwsec_sb);

	/* Frees the channel's staging buffer. */
	if (memory->staging != NULL)
		kern_free(memory->staging);

	/* Frees the files and the VBIOS. */
	nvrtx_file_unload(&gpu->booter_load);
	nvrtx_file_unload(&gpu->booter_unload);
	nvrtx_file_unload(&gpu->bootloader_file);
	nvrtx_file_unload(&gpu->falcon_bootloader);
	if (gpu->vbios_data != NULL)
		kern_free(gpu->vbios_data);
	gpu->vbios_data = NULL;

	/* Forgets everything, so a second release finds nothing. */
	kern_memset(memory, 0, sizeof(*memory));
}

/* Runs the steps of P0 in order; the first failure stops it. */
static int
gsp_prepare(
	struct nvrtx_gpu *gpu)
{
	int error;

	/* Reads the small firmware files and checks their containers. */
	error = gsp_load_files(gpu);
	if (error != 0)
		return error;

	/* Reads the GSP image into DMA memory and builds its three-level table. */
	error = gsp_prepare_image(gpu);
	if (error != 0)
		return error;

	/* Places the firmware in the VRAM and writes the placement record. */
	error = gsp_prepare_placement(gpu);
	if (error != 0)
		return error;

	/* Prepares the GSP's start page, logs, arguments and queues. */
	error = gsp_prepare_start(gpu);
	if (error != 0)
		return error;

	/* Reads the VBIOS and makes the two FWSEC programs. */
	error = gsp_prepare_fwsec(gpu);
	if (error != 0)
		return error;

	/* Succeeded: P1 has everything it needs. */
	return 0;
}

/* Reads the small firmware files and parses their containers. */
static int
gsp_load_files(
	struct nvrtx_gpu *gpu)
{
	int error;

	/* Reads the GSP's RISC-V bootloader. */
	error = nvrtx_file_load(GSP_FILE_BOOTLOADER, GSP_SMALL_FILE_MAX, &gpu->bootloader_file);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail: %s not read (%d); is nvidia-gsp-firmware installed?", GSP_FILE_BOOTLOADER, error);
		return error;
	}

	error = nvrtx_bootloader_parse(gpu->bootloader_file.data, gpu->bootloader_file.size, &gpu->bootloader_desc);
	if (error != 0)
		return error;

	/* Reads the two booters the secure processor runs. */
	error = nvrtx_file_load(GSP_FILE_BOOTER_LOAD, GSP_SMALL_FILE_MAX, &gpu->booter_load);
	if (error != 0)
		return error;

	error = nvrtx_booter_parse(gpu->booter_load.data, gpu->booter_load.size, &gpu->load);
	if (error != 0)
		return error;

	error = nvrtx_file_load(GSP_FILE_BOOTER_UNLOAD, GSP_SMALL_FILE_MAX, &gpu->booter_unload);
	if (error != 0)
		return error;

	error = nvrtx_booter_parse(gpu->booter_unload.data, gpu->booter_unload.size, &gpu->unload);
	if (error != 0)
		return error;

	/* Reads the falcon bootloader that loads FWSEC. */
	error = nvrtx_file_load(GSP_FILE_FALCON_BOOTLOADER, GSP_SMALL_FILE_MAX, &gpu->falcon_bootloader);
	if (error != 0)
		return error;

	/* Succeeded: the small files are read. */
	nvrtx_stage_mark("P0 files: booters %zu/%zu bytes, bootloader %zu bytes",
	    gpu->booter_load.size,
	    gpu->booter_unload.size,
	    gpu->bootloader_file.size);
	return 0;
}

/* Reads the GSP image into its chunks and builds the table that describes it. */
static int
gsp_prepare_image(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_boot_memory *memory;
	size_t table_pages;
	int error;

	memory = &gpu->memory;

	/* Reads the image and its signature into DMA memory. */
	error = nvrtx_file_load_image(gpu, GSP_FILE_IMAGE);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail: %s not read (%d)", GSP_FILE_IMAGE, error);
		return error;
	}

	/* Allocates the table's pages in one buffer. */
	table_pages = nvrtx_radix3_pages(memory->image.page_count);
	error = nvrtx_buffer_alloc(gpu, table_pages * NVRTX_GSP_PAGE_SIZE, &memory->table_buffer);
	if (error != 0)
		return error;

	memory->table.chunk_count = 1U;
	memory->table.chunk_bytes = table_pages * NVRTX_GSP_PAGE_SIZE;
	memory->table.page_count = table_pages;
	memory->table_cpu = (uint8_t *)memory->table_buffer.dma.address;
	memory->table_bus = memory->table_buffer.dma.device_address;
	memory->table.chunk_cpu = &memory->table_cpu;
	memory->table.chunk_bus = &memory->table_bus;

	/* Fills the table. */
	error = nvrtx_radix3_fill(&memory->image, &memory->table);
	if (error != 0)
		return error;

	/* Succeeded: the booter can find every page of the image. */
	nvrtx_stage_mark("P0 image: %zu bytes in %zu chunks, signature %zu bytes",
	    memory->image_size,
	    memory->image.chunk_count,
	    memory->signature_size);
	return 0;
}

/* Places the firmware in the VRAM and writes the record the booter reads. */
static int
gsp_prepare_placement(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_boot_memory *memory;
	struct nvrtx_wpr_sources sources;
	uint64_t vram_size;
	uint64_t vga;
	int error;

	memory = &gpu->memory;

	/* Copies the bootloader's payload into DMA memory. */
	error = nvrtx_buffer_alloc(gpu, gpu->bootloader_desc.payload_size, &memory->bootloader);
	if (error != 0)
		return error;

	kern_memcpy(memory->bootloader.dma.address,
	    gpu->bootloader_file.data + gpu->bootloader_desc.payload_at,
	    gpu->bootloader_desc.payload_size);

	/* Reads the VRAM size and the VGA workspace. */
	vram_size = nvrtx_vram_size(nvrtx_rd32(gpu, NVRTX_REG_VRAM_SIZE));
	vga = nvrtx_vga_workspace(vram_size, nvrtx_rd32(gpu, NVRTX_REG_VGA_WORKSPACE));

	/* Computes the placement. */
	error = nvrtx_wpr_layout(vram_size, vga, gpu->bootloader_desc.payload_size, memory->image_size, &gpu->layout);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail: VRAM of %lluM too small", (unsigned long long)(vram_size >> 20));
		return error;
	}

	/* Writes the record. */
	error = nvrtx_buffer_alloc(gpu, NVRTX_WPRM_BYTES, &memory->record);
	if (error != 0)
		return error;

	sources.image_table = memory->table_bus;
	sources.boot_address = memory->bootloader.dma.device_address;
	sources.boot_size = gpu->bootloader_desc.payload_size;
	sources.signature = memory->signature.dma.device_address;
	sources.signature_size = memory->signature_size;
	sources.bootloader = &gpu->bootloader_desc;
	nvrtx_wpr_record((uint8_t *)memory->record.dma.address, &gpu->layout, &sources);

	/* Succeeded: the booter knows where everything goes. */
	return 0;
}

/* Prepares the GSP's start page, its three logs, the start arguments and the queues. */
static int
gsp_prepare_start(
	struct nvrtx_gpu *gpu)
{
	static const char *const log_names[3] = { "LOGINIT", "LOGINTR", "LOGRM" };
	struct nvrtx_boot_memory *memory;
	unsigned index;
	int error;

	memory = &gpu->memory;

	/* Allocates the start page. */
	error = nvrtx_buffer_alloc(gpu, NVRTX_GSP_ARGS_BYTES, &memory->libos);
	if (error != 0)
		return error;

	/* Allocates each log, lists its pages, and names it in the start page. */
	for (index = 0U; index < 3U; index++) {
		error = nvrtx_buffer_alloc(gpu, NVRTX_GSP_LOG_BYTES, &memory->logs[index]);
		if (error != 0)
			return error;

		nvrtx_log_buffer((uint8_t *)memory->logs[index].dma.address,
		    memory->logs[index].dma.device_address,
		    NVRTX_GSP_LOG_BYTES);
		nvrtx_libos_region((uint8_t *)memory->libos.dma.address,
		    index,
		    log_names[index],
		    memory->logs[index].dma.device_address,
		    NVRTX_GSP_LOG_BYTES);
	}

	/* Allocates the queues and writes their page list and the command queue's header. */
	error = nvrtx_buffer_alloc(gpu, NVRTX_QUEUE_SHARED_BYTES, &memory->queues);
	if (error != 0)
		return error;

	memory->queue_cpu = (uint8_t *)memory->queues.dma.address;
	memory->queue_bus = memory->queues.dma.device_address;
	memory->queue_pages.chunk_count = 1U;
	memory->queue_pages.chunk_bytes = NVRTX_QUEUE_SHARED_BYTES;
	memory->queue_pages.page_count = GSP_QUEUE_PTE_COUNT;
	memory->queue_pages.chunk_cpu = &memory->queue_cpu;
	memory->queue_pages.chunk_bus = &memory->queue_bus;
	nvrtx_queue_shared_init(&memory->queue_pages);

	/* Allocates the channel's staging buffer and sets the channel up. */
	memory->staging = kern_malloc(NVRTX_STAGING_BYTES);
	if (memory->staging == NULL)
		return ENOMEM;

	nvrtx_channel_init(&gpu->channel, &memory->queue_pages, memory->staging);

	/* Writes the start arguments and names them in the start page. */
	error = nvrtx_buffer_alloc(gpu, NVRTX_RMARGS_BYTES, &memory->rmargs);
	if (error != 0)
		return error;

	nvrtx_rmargs((uint8_t *)memory->rmargs.dma.address, memory->queue_bus, GSP_QUEUE_PTE_COUNT);
	nvrtx_libos_region((uint8_t *)memory->libos.dma.address,
	    3U,
	    "RMARGS",
	    memory->rmargs.dma.device_address,
	    NVRTX_RMARGS_BYTES);

	/* Succeeded: the GSP's operating system has what it starts with. */
	return 0;
}

/* Reads the VBIOS, finds FWSEC and makes the FRTS and SB programs. */
static int
gsp_prepare_fwsec(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_boot_memory *memory;
	const uint8_t *program;
	int error;

	memory = &gpu->memory;

	/* Reads the VBIOS through one of its windows. */
	error = nvrtx_rom_read(gpu);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail: VBIOS not read (%d)", error);
		return error;
	}

	/* Finds the FWSEC program. */
	error = nvrtx_vbios_fwsec(&gpu->vbios, &gpu->fwsec);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail: FWSEC not found in the VBIOS (%d)", error);
		return error;
	}

	/* Copies the program twice, once for each command. */
	program = gpu->vbios_data + gpu->fwsec.program_at;
	error = nvrtx_buffer_alloc(gpu, gpu->fwsec.program_size, &memory->fwsec_frts);
	if (error != 0)
		return error;

	kern_memcpy(memory->fwsec_frts.dma.address, program, gpu->fwsec.program_size);
	error = nvrtx_buffer_alloc(gpu, gpu->fwsec.program_size, &memory->fwsec_sb);
	if (error != 0)
		return error;

	kern_memcpy(memory->fwsec_sb.dma.address, program, gpu->fwsec.program_size);

	/* Tells the first copy to set up the region and the second to tear it down. */
	error = nvrtx_fwsec_command((uint8_t *)memory->fwsec_frts.dma.address,
	    &gpu->fwsec,
	    NVRTX_FWSEC_FRTS,
	    gpu->layout.frts,
	    gpu->layout.frts_size);
	if (error != 0)
		return error;

	error = nvrtx_fwsec_command((uint8_t *)memory->fwsec_sb.dma.address, &gpu->fwsec, NVRTX_FWSEC_SB, 0U, 0U);
	if (error != 0)
		return error;

	/* Succeeded: both programs are ready. */
	nvrtx_stage_mark("P0 vbios %zu bytes, fwsec v%u %u bytes",
	    gpu->vbios_size,
	    (unsigned)gpu->fwsec.version,
	    (unsigned)gpu->fwsec.program_size);
	return 0;
}
