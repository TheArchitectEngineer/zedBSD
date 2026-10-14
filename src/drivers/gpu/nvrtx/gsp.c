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
 * P1a waits for the GPU's own firmware to finish devinit.  P1b runs
 * FWSEC-FRTS, which sets up the protected region at the top of the VRAM;
 * from there on a failure tears the region down once (P7's FWSEC-SB and
 * booter_unload) and does not retry.  P2a queues the system information and
 * the registry, puts the GSP's falcon into RISC-V mode and runs booter_load,
 * which places the firmware in the region and starts it.  P2b waits for the
 * firmware's init-done event, performing the CPU sequencer it sends.
 *
 * P3 asks for the static information and allocates the first objects: a
 * client, the device and the subdevice.
 *
 * The stages after P3 are not in this revision: the worker stops the GSP
 * (P7: the unload call, the GSP's falcon reset, FWSEC-SB, booter_unload) and
 * gives the preparation back, so the GPU ends as the firmware left it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/pci/pci.h>
#include <kern/clock.h>
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

/* How long P1a waits for devinit, in microseconds (about 2,050 steps of 2 ms). */
#define GSP_DEVINIT_TIMEOUT_US		4100000U
#define GSP_DEVINIT_STEP_US		2000U

/* How long P2b waits for the init-done event, and P7 for the unload's reply and the GSP's halt. */
#define GSP_INIT_DONE_TIMEOUT_US	10000000U
#define GSP_UNLOAD_TIMEOUT_US		2000000U
#define GSP_UNLOADED_TIMEOUT_US		2000000U

/* The GSP falcon's mailbox 0 reads this once the firmware has unloaded. */
#define GSP_UNLOADED			0x80000000U

/* The GSP falcon's registers the start writes: the two mailboxes and the application version. */
#define GSP_REG_MAILBOX0		(NVRTX_FALCON_GSP_BASE + 0x040U)
#define GSP_REG_MAILBOX1		(NVRTX_FALCON_GSP_BASE + 0x044U)
#define GSP_REG_APP_VERSION		(NVRTX_FALCON_GSP_BASE + 0x080U)

/* The GSP's RISC-V core status: bit 0 set while it runs. */
#define GSP_REG_RISCV_STATUS		0x111240U

/* The highest user address the resource manager is told of (the 47-bit user half). */
#define GSP_USER_ADDRESS_LIMIT		0x00007ffffffff000ULL

/* The page of the queue memory that lists its pages, counted with the rest. */
#define GSP_QUEUE_PTE_COUNT		(NVRTX_QUEUE_SHARED_BYTES / NVRTX_GSP_PAGE_SIZE)

static int gsp_prepare(struct nvrtx_gpu *gpu);
static int gsp_load_files(struct nvrtx_gpu *gpu);
static int gsp_prepare_image(struct nvrtx_gpu *gpu);
static int gsp_prepare_placement(struct nvrtx_gpu *gpu);
static int gsp_prepare_start(struct nvrtx_gpu *gpu);
static int gsp_prepare_fwsec(struct nvrtx_gpu *gpu);
static int gsp_prepare_falcons(struct nvrtx_gpu *gpu);
static int gsp_devinit_wait(struct nvrtx_gpu *gpu);
static int gsp_region_set(struct nvrtx_gpu *gpu);
static void gsp_finish(struct nvrtx_gpu *gpu);
static int gsp_queue_setup_calls(struct nvrtx_gpu *gpu);
static int gsp_boot_riscv(struct nvrtx_gpu *gpu);
static int gsp_init_done(struct nvrtx_gpu *gpu);
static int gsp_stop(struct nvrtx_gpu *gpu);
static int gsp_static_info(struct nvrtx_gpu *gpu);
static int gsp_objects(struct nvrtx_gpu *gpu);

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

	/* Stops before P1a when the boot asks. */
	allowed = nvrtx_stage_allowed("P1a");
	if (!allowed) {
		nvrtx_stage_mark("P1a skipped (nvrtx.stop=P1a)");
		gsp_finish(gpu);
		return 0;
	}

	/* Waits for the GPU's firmware to finish devinit. */
	error = gsp_devinit_wait(gpu);
	if (error != 0) {
		gpu->state = NVRTX_GSP_FAILED;
		gsp_finish(gpu);
		return error;
	}

	/* Stops before P1b when the boot asks. */
	allowed = nvrtx_stage_allowed("P1b");
	if (!allowed) {
		nvrtx_stage_mark("P1b skipped (nvrtx.stop=P1b)");
		gsp_finish(gpu);
		return 0;
	}

	/* Sets the protected region up with FWSEC-FRTS. */
	error = gsp_region_set(gpu);
	if (error != 0) {
		gsp_finish(gpu);
		return error;
	}

	/* Stops before P2a when the boot asks; the finish tears the region down again. */
	allowed = nvrtx_stage_allowed("P2a");
	if (!allowed) {
		nvrtx_stage_mark("P2a skipped (nvrtx.stop=P2a)");
		gsp_finish(gpu);
		return 0;
	}

	/* Starts the GSP firmware. */
	error = gsp_boot_riscv(gpu);
	if (error != 0) {
		gsp_finish(gpu);
		return error;
	}

	/* Waits for the firmware to say it is up. */
	error = gsp_init_done(gpu);
	if (error != 0) {
		gsp_finish(gpu);
		return error;
	}

	/* Stops before P3 when the boot asks; the finish stops the GSP again. */
	allowed = nvrtx_stage_allowed("P3");
	if (!allowed) {
		nvrtx_stage_mark("P3 skipped (nvrtx.stop=P3)");
		gsp_finish(gpu);
		return 0;
	}

	/* Reads the static information and allocates the first objects. */
	error = gsp_static_info(gpu);
	if (error == 0)
		error = gsp_objects(gpu);
	if (error != 0) {
		gsp_finish(gpu);
		return error;
	}

	/* Stops before P4 when the boot asks; the finish stops the GSP again. */
	allowed = nvrtx_stage_allowed("P4");
	if (!allowed) {
		nvrtx_stage_mark("P4 skipped (nvrtx.stop=P4)");
		gsp_finish(gpu);
		return 0;
	}

	/* Makes the first address space. */
	error = nvrtx_space_start(gpu);
	if (error != 0) {
		gsp_finish(gpu);
		return error;
	}

	/* Stops before P5 when the boot asks. */
	allowed = nvrtx_stage_allowed("P5");
	if (!allowed) {
		nvrtx_stage_mark("P5 skipped (nvrtx.stop=P5)");
		gsp_finish(gpu);
		return 0;
	}

	/* Makes the first channel and runs one fence through it. */
	error = nvrtx_channel_start(gpu);
	if (error != 0) {
		gsp_finish(gpu);
		return error;
	}

	/* Says why the boot goes no further; the finish runs P6's stop and P7. */
	nvrtx_stage_mark("P8 is not in this kernel; stopping after P5");
	gsp_finish(gpu);

	/* Succeeded: the boot stopped where this revision ends. */
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

	/* Frees the buffers of the first channel. */
	nvrtx_channel_release(gpu);

	/* Frees the channel's staging buffer and the reply buffer. */
	if (memory->staging != NULL)
		kern_free(memory->staging);
	if (gpu->reply != NULL)
		kern_free(gpu->reply);
	gpu->reply = NULL;
	if (gpu->call != NULL)
		kern_free(gpu->call);
	gpu->call = NULL;
	if (gpu->static_info != NULL)
		kern_free(gpu->static_info);
	gpu->static_info = NULL;

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

	/* Finds the two falcons. */
	error = gsp_prepare_falcons(gpu);
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

	error = nvrtx_falcon_bl_parse(gpu->falcon_bootloader.data, gpu->falcon_bootloader.size, &gpu->falcon_bl);
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

/* Finds the GSP's falcon and the secure processor with its PMC bit. */
static int
gsp_prepare_falcons(
	struct nvrtx_gpu *gpu)
{
	uint32_t words[NVRTX_DEVICE_INFO_WORDS];
	uint32_t bit;
	uint32_t index;
	int error;

	/* The GSP's falcon has no PMC bit of its own. */
	gpu->gsp_falcon.base = NVRTX_FALCON_GSP_BASE;
	gpu->gsp_falcon.pmc_mask = 0U;

	/* Reads the device information table and finds the secure processor's reset bit. */
	for (index = 0U; index < NVRTX_DEVICE_INFO_WORDS; index++)
		words[index] = nvrtx_rd32(gpu, NVRTX_REG_DEVICE_INFO + index * 4U);

	error = nvrtx_device_info_reset(words, NVRTX_ENGINE_SEC2, &bit);
	if (error != 0) {
		nvrtx_stage_mark("P0 fail: no SEC2 in the device information (%d)", error);
		return error;
	}

	gpu->sec2_falcon.base = NVRTX_FALCON_SEC2_BASE;
	gpu->sec2_falcon.pmc_mask = 1U << bit;

	/* Succeeded: both falcons are known. */
	nvrtx_stage_mark("P0 falcons: SEC2 reset bit %u", (unsigned)bit);
	return 0;
}

/* Runs P1a: waits for the GPU's own firmware to finish devinit. */
static int
gsp_devinit_wait(
	struct nvrtx_gpu *gpu)
{
	uint32_t waited;
	uint32_t level;
	uint32_t progress;

	/* Polls both registers until devinit is complete or the time is spent. */
	nvrtx_stage_mark("P1a begin: devinit");
	for (waited = 0U; waited <= GSP_DEVINIT_TIMEOUT_US; waited += GSP_DEVINIT_STEP_US) {
		level = nvrtx_rd32(gpu, NVRTX_REG_DEVINIT_LEVEL);
		progress = nvrtx_rd32(gpu, NVRTX_REG_DEVINIT_PROGRESS);
		if ((level & 0x1U) != 0U && (progress & 0xffU) == NVRTX_DEVINIT_COMPLETED) {
			nvrtx_stage_mark("P1a ok after %u ms", (unsigned)(waited / 1000U));
			return 0;
		}

		kern_usleep_range(GSP_DEVINIT_STEP_US, GSP_DEVINIT_STEP_US * 2U);
	}

	/* Devinit never completed: the GPU is not used. */
	nvrtx_stage_mark("P1a fail: devinit level %08x progress %08x", (unsigned)level, (unsigned)progress);
	return ETIMEDOUT;
}

/* Runs P1b: checks that no region is there, then sets it up with FWSEC-FRTS. */
static int
gsp_region_set(
	struct nvrtx_gpu *gpu)
{
	uint32_t low;
	uint32_t high;
	uint32_t result;
	int error;

	/* A region left by an earlier boot cannot be set up again: stop without writing. */
	nvrtx_region_bounds(gpu, &low, &high);
	if (high != 0U) {
		nvrtx_stage_mark("P1b stop: region %08x-%08x already set; power cycle needed", (unsigned)low, (unsigned)high);
		gpu->state = NVRTX_GSP_FAILED;
		return EBUSY;
	}

	/* Lets the falcons reach system memory. */
	error = drv_pci_device_set_bus_master(gpu->identity.pci, true);
	if (error != 0)
		return error;

	/* Runs FWSEC-FRTS; from here on the end of the boot tears the region down. */
	gpu->region_written = 1;
	nvrtx_stage_mark("P1b begin: FWSEC-FRTS at %llx", (unsigned long long)gpu->layout.frts);
	error = nvrtx_fwsec_run(gpu, &gpu->memory.fwsec_frts);
	if (error == ETIMEDOUT) {
		gpu->state = NVRTX_GSP_FAILED_HOLD;
		nvrtx_stage_mark("P1b fail: FWSEC did not halt; memory kept, power cycle needed");
		return error;
	}

	/* FWSEC's own result and the region's bounds decide the outcome. */
	result = nvrtx_frts_result(gpu);
	nvrtx_region_bounds(gpu, &low, &high);
	if (error != 0 || result != 0U || high == 0U) {
		nvrtx_stage_mark("P1b fail: error %d result %04x region %08x-%08x", error, (unsigned)result, (unsigned)low, (unsigned)high);
		gpu->state = NVRTX_GSP_FAILED;
		return EIO;
	}

	/* Succeeded: the region is set up. */
	gpu->state = NVRTX_GSP_REGION_SET;
	nvrtx_stage_mark("P1b ok: region %08x-%08x", (unsigned)low, (unsigned)high);
	return 0;
}

/*
 * Ends the boot: tears a region down once, then gives the preparation back.
 *
 * Memory a falcon may still read (one that did not halt) is kept.
 */
static void
gsp_finish(
	struct nvrtx_gpu *gpu)
{
	bool allowed;
	int error;

	/* Keeps everything when a falcon may still be reading the memory. */
	if (gpu->state == NVRTX_GSP_FAILED_HOLD)
		return;

	/* Frees the channel and the address space (P6) unless the boot stopped before P6. */
	if (gpu->state == NVRTX_GSP_RUNNING) {
		allowed = nvrtx_stage_allowed("P6");
		if (allowed) {
			nvrtx_channel_stop(gpu);
		} else {
			nvrtx_stage_mark("P6 skipped (nvrtx.stop=P6); the unload frees the objects");
		}
	}

	/* Stops a GSP that runs; one that does not stop keeps its memory. */
	if (gpu->state == NVRTX_GSP_RUNNING) {
		error = gsp_stop(gpu);
		if (error != 0) {
			gpu->state = NVRTX_GSP_FAILED_HOLD;
			nvrtx_stage_mark("P7 fail: the GSP did not stop (%d); memory kept, power cycle needed", error);
			return;
		}
	}

	/* Tears down a region this boot set up, or one a failed FRTS left. */
	error = 0;
	if (gpu->region_written != 0)
		error = nvrtx_region_tear_down(gpu);
	if (error == ETIMEDOUT) {
		gpu->state = NVRTX_GSP_FAILED_HOLD;
		nvrtx_stage_mark("P7 fail: a program did not halt; memory kept, power cycle needed");
		return;
	}

	/* Gives the preparation back; a region that stayed is left for a power cycle. */
	nvrtx_gsp_release(gpu);
}

/* Queues the two calls the firmware reads first: the system information and the registry. */
static int
gsp_queue_setup_calls(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_sysinfo_values values;
	struct nvrtx_device *identity;
	struct drv_pci_device *pci;
	uint8_t *record;
	size_t length;
	uint32_t sequence;
	int error;

	identity = &gpu->identity;
	pci = identity->pci;

	/* Describes the GPU's windows and PCI identity. */
	values.registers = identity->windows[0].bus_address;
	values.vram = identity->windows[1].bus_address;
	values.instance = identity->windows[2].bus_address;
	values.bus_device_function = ((uint64_t)identity->address.bus << 8) |
	    ((uint64_t)identity->address.device << 3) |
	    identity->address.function;
	values.user_address_limit = GSP_USER_ADDRESS_LIMIT;
	values.vendor = drv_pci_device_vendor(pci);
	values.device = drv_pci_device_product(pci);
	values.subvendor = drv_pci_device_subvendor(pci);
	values.subdevice = drv_pci_device_subproduct(pci);
	values.revision = drv_pci_device_revision(pci);
	values.primary = identity->gop_on_gpu;

	/* Builds both payloads in the reply buffer, one after the other, and queues them unnumbered. */
	record = gpu->reply;
	nvrtx_sysinfo(record, &values);
	error = nvrtx_rpc_send(gpu, NVRTX_CALL_SYSTEM_INFO, record, NVRTX_SYSINFO_BYTES, 0, &sequence);
	if (error != 0)
		return error;

	error = nvrtx_registry(record, NVRTX_STAGING_BYTES, &length);
	if (error != 0)
		return error;

	error = nvrtx_rpc_send(gpu, NVRTX_CALL_REGISTRY, record, length, 0, &sequence);
	if (error != 0)
		return error;

	/* Succeeded: the firmware will read both as soon as it runs. */
	return 0;
}

/* Runs P2a: queues the first calls, puts the GSP in RISC-V mode, and starts it through booter_load. */
static int
gsp_boot_riscv(
	struct nvrtx_gpu *gpu)
{
	uint64_t libos;
	uint64_t record;
	uint32_t status;
	int error;

	nvrtx_stage_mark("P2a begin: booter_load");

	/* Allocates the buffer each received message is copied into, and the one calls are built in. */
	gpu->reply = kern_malloc(NVRTX_STAGING_BYTES);
	if (gpu->reply == NULL)
		return ENOMEM;

	gpu->call = kern_malloc(NVRTX_STAGING_BYTES);
	if (gpu->call == NULL)
		return ENOMEM;

	/* Queues the system information and the registry. */
	error = gsp_queue_setup_calls(gpu);
	if (error != 0)
		return error;

	/* Puts the GSP's falcon into RISC-V mode and gives it the start page. */
	error = nvrtx_falcon_reset_engine(gpu, &gpu->gsp_falcon);
	if (error != 0)
		return error;

	libos = gpu->memory.libos.dma.device_address;
	nvrtx_wr32(gpu, GSP_REG_MAILBOX0, (uint32_t)(libos & 0xffffffffU));
	nvrtx_wr32(gpu, GSP_REG_MAILBOX1, (uint32_t)(libos >> 32));

	/* Runs booter_load with the placement record's address. */
	record = gpu->memory.record.dma.device_address;
	error = nvrtx_booter_run(gpu, &gpu->load, (uint32_t)(record & 0xffffffffU), (uint32_t)(record >> 32));
	if (error == ETIMEDOUT) {
		gpu->state = NVRTX_GSP_FAILED_HOLD;
		nvrtx_stage_mark("P2a fail: booter_load did not halt; memory kept, power cycle needed");
		return error;
	}

	if (error != 0) {
		nvrtx_stage_mark("P2a fail: booter_load (%d)", error);
		return error;
	}

	/* Tells the GSP the bootloader's version and checks that its RISC-V core runs. */
	nvrtx_wr32(gpu, GSP_REG_APP_VERSION, gpu->bootloader_desc.app_version);
	status = nvrtx_rd32(gpu, GSP_REG_RISCV_STATUS);
	if ((status & 0x1U) == 0U) {
		nvrtx_stage_mark("P2a fail: the RISC-V core is not running (%08x)", (unsigned)status);
		return EIO;
	}

	/* Succeeded: the GSP firmware runs; from here the end of the boot must stop it. */
	gpu->state = NVRTX_GSP_RUNNING;
	nvrtx_stage_mark("P2a ok: RISC-V running");
	return 0;
}

/* Runs P2b: waits for the init-done event, performing the sequencer meanwhile. */
static int
gsp_init_done(
	struct nvrtx_gpu *gpu)
{
	size_t length;
	uint8_t *log;
	int error;

	/* Waits for the event. */
	nvrtx_stage_mark("P2b begin: waiting for init done");
	error = nvrtx_rpc_wait(gpu, NVRTX_EVENT_INIT_DONE, 0U, GSP_INIT_DONE_TIMEOUT_US, &length);
	if (error != 0) {
		log = (uint8_t *)gpu->memory.logs[0].dma.address;
		nvrtx_stage_mark("P2b fail (%d): init log put %llx", error, (unsigned long long)nvrtx_get64(log, 0U));
		return error;
	}

	/* Succeeded: the firmware is up. */
	nvrtx_stage_mark("P2b ok: GSP-RM is up");
	return 0;
}

/*
 * Stops a running GSP (P7): the unload call, the halt, and the falcon reset.
 *
 * The region is torn down afterwards by the finish.  Returns ETIMEDOUT when
 * the firmware did not unload in time.
 */
static int
gsp_stop(
	struct nvrtx_gpu *gpu)
{
	uint8_t args[NVRTX_UNLOAD_ARGS_BYTES];
	uint32_t sequence;
	size_t length;
	int error;

	/* Asks the firmware to unload and waits for its reply; a lost reply still goes on to the halt check. */
	nvrtx_stage_mark("P7 begin: unload");
	nvrtx_unload_args(args);
	error = nvrtx_rpc_send(gpu, NVRTX_CALL_UNLOAD, args, sizeof(args), 1, &sequence);
	if (error == 0)
		error = nvrtx_rpc_wait(gpu, NVRTX_CALL_UNLOAD, sequence, GSP_UNLOAD_TIMEOUT_US, &length);
	if (error != 0)
		nvrtx_stage_mark("P7 unload call: %d", error);

	/* Waits for the firmware to say it has unloaded. */
	error = nvrtx_poll(gpu, GSP_REG_MAILBOX0, 0xffffffffU, GSP_UNLOADED, GSP_UNLOADED_TIMEOUT_US);
	if (error != 0)
		return error;

	/* Resets the GSP's falcon; the firmware no longer reads the memory. */
	error = nvrtx_falcon_reset(gpu, &gpu->gsp_falcon);
	if (error != 0)
		return error;

	/* Succeeded: the GSP is stopped. */
	gpu->state = NVRTX_GSP_REGION_SET;
	return 0;
}

/* Asks the firmware for its static information and marks what it says about the GPU. */
static int
gsp_static_info(
	struct nvrtx_gpu *gpu)
{
	char name[NVRTX_STATIC_NAME_BYTES + 1U];
	uint32_t sequence;
	size_t length;
	size_t index;
	int error;

	/* Sends a zeroed record of the information's size and waits for it to come back filled. */
	nvrtx_stage_mark("P3 begin: static info");
	kern_memset(gpu->reply, 0, NVRTX_STATIC_INFO_BYTES);
	error = nvrtx_rpc_send(gpu, NVRTX_CALL_STATIC_INFO, gpu->reply, NVRTX_STATIC_INFO_BYTES, 1, &sequence);
	if (error != 0)
		return error;

	error = nvrtx_rpc_wait(gpu, NVRTX_CALL_STATIC_INFO, sequence, NVRTX_CALL_TIMEOUT_US, &length);
	if (error != 0) {
		nvrtx_stage_mark("P3 fail: static info (%d)", error);
		return error;
	}

	/* A short reply is not the 570.144 layout. */
	if (length < NVRTX_STATIC_INFO_BYTES) {
		nvrtx_stage_mark("P3 fail: static info of %zu bytes", length);
		return EIO;
	}

	/* Keeps the information: P4 reads the FB regions and the internal handles from it. */
	gpu->static_info = kern_malloc(NVRTX_STATIC_INFO_BYTES);
	if (gpu->static_info == NULL)
		return ENOMEM;

	kern_memcpy(gpu->static_info, gpu->reply, NVRTX_STATIC_INFO_BYTES);
	gpu->internal_client = nvrtx_get32(gpu->static_info, NVRTX_STATIC_INTERNAL_CLIENT_AT);
	gpu->internal_subdevice = nvrtx_get32(gpu->static_info, NVRTX_STATIC_INTERNAL_CLIENT_AT + 8U);

	/* Copies the GPU's short name, keeping printable characters only. */
	for (index = 0U; index < NVRTX_STATIC_NAME_BYTES; index++) {
		name[index] = (char)gpu->reply[NVRTX_STATIC_SHORT_NAME_AT + index];
		if (name[index] == '\0')
			break;
		if (name[index] < ' ' || name[index] > '~')
			name[index] = '?';
	}
	name[index] = '\0';

	/* Succeeded: the firmware knows the GPU. */
	nvrtx_stage_mark("P3 static: %s vram %lluM uefi %u", name,
	    (unsigned long long)(nvrtx_get64(gpu->reply, NVRTX_STATIC_FB_LENGTH_AT) >> 20),
	    (unsigned)gpu->reply[NVRTX_STATIC_UEFI_AT]);
	nvrtx_stage_mark("P3 static: bar1 root %llx bar2 root %llx",
	    (unsigned long long)nvrtx_get64(gpu->reply, NVRTX_STATIC_BAR1_ROOT_AT),
	    (unsigned long long)nvrtx_get64(gpu->reply, NVRTX_STATIC_BAR2_ROOT_AT));
	return 0;
}

/* Allocates the client, the device and the subdevice. */
static int
gsp_objects(
	struct nvrtx_gpu *gpu)
{
	uint8_t parameters[NVRTX_ROOT_PARAMS_BYTES];
	int error;

	/* The client is its own parent; it names itself and no process. */
	kern_memset(parameters, 0, sizeof(parameters));
	nvrtx_put32(parameters, 0U, NVRTX_HANDLE_CLIENT);
	nvrtx_put32(parameters, NVRTX_ROOT_PROCESS_AT, 0xffffffffU);
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_CLIENT, NVRTX_CLASS_ROOT, parameters, NVRTX_ROOT_PARAMS_BYTES);
	if (error != 0)
		return error;

	/* The device shares the client. */
	kern_memset(parameters, 0, sizeof(parameters));
	nvrtx_put32(parameters, NVRTX_DEVICE_SHARE_AT, NVRTX_HANDLE_CLIENT);
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_DEVICE, NVRTX_CLASS_DEVICE, parameters, NVRTX_DEVICE_PARAMS_BYTES);
	if (error != 0)
		return error;

	/* The subdevice is the first (and only) one of the device. */
	kern_memset(parameters, 0, sizeof(parameters));
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_DEVICE, NVRTX_HANDLE_SUBDEVICE, NVRTX_CLASS_SUBDEVICE, parameters, NVRTX_SUBDEVICE_PARAMS_BYTES);
	if (error != 0)
		return error;

	/* Succeeded: the driver has its client, device and subdevice. */
	nvrtx_stage_mark("P3 ok: client, device and subdevice");
	return 0;
}
