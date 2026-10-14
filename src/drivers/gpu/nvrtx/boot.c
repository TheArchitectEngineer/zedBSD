/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Running the signed programs of the GSP boot.
 *
 * FWSEC runs on the GSP's falcon.  It is too large for the falcon's
 * memories to be written by the driver, so the small falcon bootloader is
 * written at the top of the instruction memory with a descriptor in the data
 * memory, and it copies FWSEC from system memory by DMA.  The booters run
 * on the secure processor (SEC2) and are written whole: the non-secure code,
 * the secure code, and the data, with the production signature patched in.
 *
 * The protected region FWSEC sets up (FRTS) is torn down by FWSEC's SB
 * command and then by booter_unload, once each, after the GSP firmware has
 * unloaded; a region that stays is reported and left for a power cycle
 * (plan/ws037/nvrtx-design.md, 3.1).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The DMA context the falcon bootloader reads FWSEC through. */
#define BOOT_DMA_CONTEXT_SYSTEM		4U

/*
 * The descriptor the falcon bootloader reads from data memory 0, 84 bytes:
 * the DMA context, the code's bus address, the non-secure and secure code
 * ranges in the program, the entry point, and the data's bus address and size.
 */
#define BOOT_BL_DESC_BYTES		84U
#define BOOT_BL_CONTEXT_AT		32U
#define BOOT_BL_CODE_BUS_AT		36U
#define BOOT_BL_CODE_AT			44U
#define BOOT_BL_CODE_SIZE_AT		48U
#define BOOT_BL_SECURE_AT		52U
#define BOOT_BL_SECURE_SIZE_AT		56U
#define BOOT_BL_ENTRY_AT		60U
#define BOOT_BL_DATA_BUS_AT		64U
#define BOOT_BL_DATA_SIZE_AT		72U

/* The secure processor's debug-fuse register: bit 20 set on a debug part, which takes the second signature. */
#define BOOT_SEC2_DEBUG			0x408U
#define BOOT_SEC2_DEBUG_FUSED		(1U << 20)

/* The scratch registers FWSEC reports in: FRTS in the high half of 0x1438, SB in the low half of 0x1454. */
#define BOOT_REG_FRTS_RESULT		0x001438U
#define BOOT_REG_SB_RESULT		0x001454U

/* The protected region's bounds (VRAM address bits 31:4 in 4 KiB units). */
#define BOOT_REG_REGION_LOW		0x1fa824U
#define BOOT_REG_REGION_HIGH		0x1fa828U

/* The mailbox values booter_unload takes for a plain unload. */
#define BOOT_UNLOAD_ARGUMENT		0xffU

/*
 * Runs one FWSEC program (FRTS or SB) on the GSP's falcon.
 *
 * Returns 0 when the falcon halted with mailbox 0 at zero, EIO when it
 * halted with an error there, or ETIMEDOUT when it did not halt.
 */
int
nvrtx_fwsec_run(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_buffer *program)
{
	const struct nvrtx_falcon *falcon;
	const struct nvrtx_fwsec *fwsec;
	uint8_t descriptor[BOOT_BL_DESC_BYTES];
	uint32_t limit;
	uint32_t mailbox0;
	uint32_t mailbox1;
	size_t at;
	int error;

	falcon = &gpu->gsp_falcon;
	fwsec = &gpu->fwsec;

	/* Resets the falcon and lets it load from physical memory. */
	error = nvrtx_falcon_reset(gpu, falcon);
	if (error != 0)
		return error;

	nvrtx_falcon_load_physical(gpu, falcon);

	/* Writes the falcon bootloader at the top of the instruction memory. */
	limit = nvrtx_falcon_code_limit(gpu, falcon);
	if (limit < gpu->falcon_bl.code_size)
		return EINVAL;

	error = nvrtx_falcon_imem(gpu,
	    falcon,
	    gpu->falcon_bl.code,
	    gpu->falcon_bl.code_size,
	    limit - gpu->falcon_bl.code_size,
	    gpu->falcon_bl.start_tag,
	    0);
	if (error != 0)
		return error;

	/* Builds the descriptor that tells the bootloader where FWSEC is. */
	for (at = 0U; at < sizeof(descriptor); at++)
		descriptor[at] = 0U;
	nvrtx_put32(descriptor, BOOT_BL_CONTEXT_AT, BOOT_DMA_CONTEXT_SYSTEM);
	nvrtx_put64(descriptor, BOOT_BL_CODE_BUS_AT, program->dma.device_address);
	nvrtx_put32(descriptor, BOOT_BL_CODE_AT, fwsec->imem_base);
	nvrtx_put32(descriptor, BOOT_BL_CODE_SIZE_AT, fwsec->imem_size - fwsec->imem_secure_size);
	nvrtx_put32(descriptor, BOOT_BL_SECURE_AT, fwsec->imem_secure_base);
	nvrtx_put32(descriptor, BOOT_BL_SECURE_SIZE_AT, fwsec->imem_secure_size);
	nvrtx_put32(descriptor, BOOT_BL_ENTRY_AT, 0U);
	nvrtx_put64(descriptor, BOOT_BL_DATA_BUS_AT, program->dma.device_address + fwsec->dmem_at);
	nvrtx_put32(descriptor, BOOT_BL_DATA_SIZE_AT, fwsec->dmem_size);

	/* Points the bootloader's DMA context at system memory and writes the descriptor. */
	nvrtx_falcon_dma_system(gpu, falcon, BOOT_DMA_CONTEXT_SYSTEM);
	error = nvrtx_falcon_dmem(gpu, falcon, descriptor, sizeof(descriptor), 0U);
	if (error != 0)
		return error;

	/* Starts the bootloader at its tag's address and waits for FWSEC to halt. */
	mailbox0 = 0U;
	mailbox1 = 0U;
	error = nvrtx_falcon_boot(gpu, falcon, gpu->falcon_bl.start_tag << 8, &mailbox0, &mailbox1);
	if (error != 0) {
		nvrtx_stage_mark("fwsec: no halt (mailbox %08x %08x)", (unsigned)mailbox0, (unsigned)mailbox1);
		return error;
	}

	/* A nonzero mailbox is FWSEC's error. */
	if (mailbox0 != 0U) {
		nvrtx_stage_mark("fwsec: mailbox %08x %08x", (unsigned)mailbox0, (unsigned)mailbox1);
		return EIO;
	}

	/* Succeeded: FWSEC ran its command. */
	return 0;
}

/*
 * Runs a booter on the secure processor with two mailbox arguments.
 *
 * Returns 0 when the processor halted with mailbox 0 at zero, EIO when it
 * halted with an error there, or ETIMEDOUT when it did not halt.
 */
int
nvrtx_booter_run(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_booter *booter,
	uint32_t mailbox0,
	uint32_t mailbox1)
{
	const struct nvrtx_falcon *falcon;
	uint8_t *payload;
	uint32_t debug;
	unsigned index;
	size_t signature_at;
	int error;

	falcon = &gpu->sec2_falcon;

	/* Resets the processor. */
	error = nvrtx_falcon_reset(gpu, falcon);
	if (error != 0)
		return error;

	/* A debug-fused part takes the second signature. */
	debug = nvrtx_rd32(gpu, falcon->base + BOOT_SEC2_DEBUG);
	index = 0U;
	if ((debug & BOOT_SEC2_DEBUG_FUSED) != 0U)
		index = 1U;

	error = nvrtx_booter_signature(booter, index, &signature_at);
	if (error != 0)
		return error;

	/* Copies the payload and patches the signature into it. */
	payload = kern_malloc(booter->payload_size);
	if (payload == NULL)
		return ENOMEM;

	kern_memcpy(payload, booter->file + booter->payload_at, booter->payload_size);
	kern_memcpy(payload + booter->patch_location, booter->file + signature_at, booter->signature_size);

	/* Writes the non-secure code, the secure code after it, and the data. */
	nvrtx_falcon_load_physical(gpu, falcon);
	error = nvrtx_falcon_imem(gpu, falcon, payload, booter->os_code_size, booter->os_code_at, booter->os_code_at >> 8, 0);
	if (error == 0) {
		error = nvrtx_falcon_imem(gpu,
		    falcon,
		    payload + booter->os_code_size,
		    booter->app_size,
		    booter->app_at,
		    booter->app_at >> 8,
		    1);
	}

	if (error == 0)
		error = nvrtx_falcon_dmem(gpu, falcon, payload + booter->os_data_at, booter->os_data_size, 0U);
	kern_free(payload);
	if (error != 0)
		return error;

	/* Starts the booter and waits for it to halt. */
	error = nvrtx_falcon_boot(gpu, falcon, booter->os_code_at, &mailbox0, &mailbox1);
	if (error != 0) {
		nvrtx_stage_mark("booter: no halt (mailbox %08x %08x)", (unsigned)mailbox0, (unsigned)mailbox1);
		return error;
	}

	/* A nonzero mailbox is the booter's error. */
	if (mailbox0 != 0U) {
		nvrtx_stage_mark("booter: mailbox %08x %08x", (unsigned)mailbox0, (unsigned)mailbox1);
		return EIO;
	}

	/* Succeeded: the booter ran. */
	return 0;
}

/*
 * Tears the protected region down: FWSEC-SB, then booter_unload, once each.
 *
 * Returns 0 when the region is gone (or was never there), EIO when it
 * stays, or ETIMEDOUT when a program did not halt (its memory must then be
 * kept until the power is cut).
 */
int
nvrtx_region_tear_down(
	struct nvrtx_gpu *gpu)
{
	uint32_t high;
	uint32_t result;
	int error;

	/* Nothing to tear down when the region is not there. */
	high = nvrtx_rd32(gpu, BOOT_REG_REGION_HIGH);
	if (high == 0U)
		return 0;

	/* Runs FWSEC's SB command and reads its result. */
	nvrtx_stage_mark("P7 begin: FWSEC-SB");
	error = nvrtx_fwsec_run(gpu, &gpu->memory.fwsec_sb);
	if (error == ETIMEDOUT)
		return error;

	result = nvrtx_rd32(gpu, BOOT_REG_SB_RESULT) & 0xffffU;
	if (error != 0 || result != 0U)
		nvrtx_stage_mark("P7 FWSEC-SB error %d result %04x", error, (unsigned)result);

	/* Runs booter_unload with the plain-unload arguments. */
	nvrtx_stage_mark("P7 booter_unload");
	error = nvrtx_booter_run(gpu, &gpu->unload, BOOT_UNLOAD_ARGUMENT, BOOT_UNLOAD_ARGUMENT);
	if (error == ETIMEDOUT)
		return error;

	/* The region must be gone now. */
	high = nvrtx_rd32(gpu, BOOT_REG_REGION_HIGH);
	if (high != 0U) {
		nvrtx_stage_mark("P7 fail: region still up to %08x; power cycle needed", (unsigned)high);
		return EIO;
	}

	/* Succeeded: the region is torn down. */
	nvrtx_stage_mark("P7 ok: region torn down");
	return 0;
}

/*
 * Reads the protected region's bounds.
 */
void
nvrtx_region_bounds(
	struct nvrtx_gpu *gpu,
	uint32_t *low,
	uint32_t *high)
{
	/* Reads the two bound registers. */
	*low = nvrtx_rd32(gpu, BOOT_REG_REGION_LOW);
	*high = nvrtx_rd32(gpu, BOOT_REG_REGION_HIGH);
}

/*
 * Reads FWSEC-FRTS's result from its scratch register: zero is success.
 */
uint32_t
nvrtx_frts_result(
	struct nvrtx_gpu *gpu)
{
	uint32_t value;

	/* The result is the register's high half. */
	value = nvrtx_rd32(gpu, BOOT_REG_FRTS_RESULT);

	/* Succeeded: the result code. */
	return value >> 16;
}
