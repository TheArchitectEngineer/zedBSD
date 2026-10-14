/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The falcon microcontrollers the GSP boot drives: the GSP's own falcon,
 * which runs FWSEC, and the secure processor (SEC2), which runs the booters.
 *
 * A falcon is reset, its instruction memory (IMEM, in 256-byte blocks that
 * each carry a tag) and data memory (DMEM) are written word by word through
 * their port registers, a start address is set, and the CPU is started;
 * the driver then waits for it to halt and reads its two mailboxes.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The falcon registers, relative to the falcon's base. */
#define FALCON_IRQ_CLEAR		0x004U
#define FALCON_IRQ_MASK_CLEAR		0x014U
#define FALCON_SCRUB_TOUCH		0x040U
#define FALCON_MAILBOX0			0x040U
#define FALCON_MAILBOX1			0x044U
#define FALCON_IRQ_DESTINATION		0x048U
#define FALCON_OS_VERSION		0x080U
#define FALCON_IDENTITY_COPY		0x084U
#define FALCON_CPU_CONTROL		0x100U
#define FALCON_BOOT_VECTOR		0x104U
#define FALCON_HARDWARE_CONFIG		0x108U
#define FALCON_DMA_CONTROL		0x10cU
#define FALCON_IMEM_CONTROL		0x180U
#define FALCON_IMEM_DATA		0x184U
#define FALCON_IMEM_TAG			0x188U
#define FALCON_DMEM_CONTROL		0x1c0U
#define FALCON_DMEM_DATA		0x1c4U
#define FALCON_ENGINE_RESET		0x3c0U
#define FALCON_DMA_CONTEXT		0x600U
#define FALCON_FBIF_CONTROL		0x624U

/* Bits of those registers. */
#define FALCON_CPU_START		0x2U
#define FALCON_CPU_HALTED		0x10U
#define FALCON_SCRUBBING		0x6U
#define FALCON_PORT_AUTO_INCREMENT	(1U << 24)
#define FALCON_PORT_SECURE		(1U << 28)
#define FALCON_FBIF_ALLOW_PHYSICAL	0x80U
#define FALCON_DMA_CONTEXT_MASK		0x7U
#define FALCON_DMA_CONTEXT_SYSTEM	0x5U
#define FALCON_CODE_LIMIT_MASK		0x1ffU
#define FALCON_IMEM_BLOCK		0x100U

/* The PMC enable register, where SEC2 has a reset bit. */
#define FALCON_PMC_ENABLE		0x000200U

/* How long the waits last, in microseconds, and the step of the poll. */
#define FALCON_SCRUB_TIMEOUT_US		10000U
#define FALCON_HALT_TIMEOUT_US		2000000U
#define FALCON_POLL_STEP_US		10U

static void falcon_mask(struct nvrtx_gpu *gpu, uint32_t offset, uint32_t mask, uint32_t value);
static int falcon_scrub_wait(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);

/*
 * Waits until a register's masked value equals the expected one.
 *
 * Returns 0, or ETIMEDOUT when the time ran out first.
 */
int
nvrtx_poll(
	struct nvrtx_gpu *gpu,
	uint32_t offset,
	uint32_t mask,
	uint32_t value,
	uint32_t timeout_us)
{
	uint32_t waited;
	uint32_t current;

	/* Reads the register until it matches or the time is spent. */
	for (waited = 0U; waited <= timeout_us; waited += FALCON_POLL_STEP_US) {
		current = nvrtx_rd32(gpu, offset);
		if ((current & mask) == value)
			return 0;

		kern_usleep_range(FALCON_POLL_STEP_US, FALCON_POLL_STEP_US * 2U);
	}

	/* The register never matched. */
	return ETIMEDOUT;
}

/*
 * Resets a falcon: disables it (and its PMC bit), then enables it again.
 *
 * Returns ETIMEDOUT when its memories are not scrubbed in time.
 */
int
nvrtx_falcon_reset(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon)
{
	uint32_t identity;
	int error;

	/* Stops its interrupts from reaching the host and clears their masks. */
	falcon_mask(gpu, falcon->base + FALCON_IRQ_DESTINATION, 0x3U, 0U);
	nvrtx_wr32(gpu, falcon->base + FALCON_IRQ_MASK_CLEAR, 0xffffffffU);

	/* Takes a falcon with a PMC bit out of the enable register. */
	if (falcon->pmc_mask != 0U)
		falcon_mask(gpu, FALCON_PMC_ENABLE, falcon->pmc_mask, 0U);

	/* Pulses the engine reset. */
	error = nvrtx_falcon_reset_engine(gpu, falcon);
	if (error != 0)
		return error;

	/* Pulses it again for the enable, puts the PMC bit back and waits for the scrub. */
	error = nvrtx_falcon_reset_engine(gpu, falcon);
	if (error != 0)
		return error;

	if (falcon->pmc_mask != 0U) {
		falcon_mask(gpu, FALCON_PMC_ENABLE, falcon->pmc_mask, falcon->pmc_mask);
		(void)nvrtx_rd32(gpu, FALCON_PMC_ENABLE);
	}

	error = falcon_scrub_wait(gpu, falcon);
	if (error != 0)
		return error;

	/* Gives the falcon a copy of the chip identity, as it expects. */
	identity = nvrtx_rd32(gpu, NVRTX_REG_MC_IDENT_0);
	nvrtx_wr32(gpu, falcon->base + FALCON_IDENTITY_COPY, identity);

	/* Succeeded: the falcon is reset with clean memories. */
	return 0;
}

/*
 * Prepares a reset falcon to load without an instance block: physical DMA allowed, DMA control cleared.
 */
void
nvrtx_falcon_load_physical(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon)
{
	/* Lets the falcon's DMA use physical addresses, with no context bound. */
	falcon_mask(gpu, falcon->base + FALCON_FBIF_CONTROL, FALCON_FBIF_ALLOW_PHYSICAL, FALCON_FBIF_ALLOW_PHYSICAL);
	nvrtx_wr32(gpu, falcon->base + FALCON_DMA_CONTROL, 0U);
}

/*
 * Points one DMA context of a falcon at non-coherent system memory.
 */
void
nvrtx_falcon_dma_system(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon,
	uint32_t context)
{
	/* Sets the context's target to physical system memory, non-coherent. */
	falcon_mask(gpu, falcon->base + FALCON_DMA_CONTEXT + context * 4U, FALCON_DMA_CONTEXT_MASK, FALCON_DMA_CONTEXT_SYSTEM);
}

/*
 * Reports the size of a falcon's instruction memory.
 */
uint32_t
nvrtx_falcon_code_limit(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon)
{
	uint32_t config;

	/* The hardware configuration holds the size in 256-byte blocks. */
	config = nvrtx_rd32(gpu, falcon->base + FALCON_HARDWARE_CONFIG);

	/* Succeeded: the size in bytes. */
	return (config & FALCON_CODE_LIMIT_MASK) << 8;
}

/*
 * Writes code into a falcon's instruction memory.
 *
 * The length is a whole number of 256-byte blocks; each block carries the
 * next tag, starting from the given one.  Secure code is marked so.
 */
int
nvrtx_falcon_imem(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon,
	const uint8_t *code,
	uint32_t length,
	uint32_t address,
	uint32_t tag,
	int secure)
{
	uint32_t control;
	uint32_t at;

	/* Refuses a length that is not whole blocks. */
	if (length == 0U || (length % FALCON_IMEM_BLOCK) != 0U)
		return EINVAL;

	/* Opens the port at the address, incrementing, marked secure when asked. */
	control = FALCON_PORT_AUTO_INCREMENT | address;
	if (secure != 0)
		control |= FALCON_PORT_SECURE;
	nvrtx_wr32(gpu, falcon->base + FALCON_IMEM_CONTROL, control);

	/* Writes each block's tag, then its words. */
	for (at = 0U; at < length; at += 4U) {
		if ((at % FALCON_IMEM_BLOCK) == 0U) {
			nvrtx_wr32(gpu, falcon->base + FALCON_IMEM_TAG, tag);
			tag++;
		}

		nvrtx_wr32(gpu, falcon->base + FALCON_IMEM_DATA, nvrtx_get32(code, at));
	}

	/* Succeeded: the code is in the instruction memory. */
	return 0;
}

/*
 * Writes data into a falcon's data memory.
 */
int
nvrtx_falcon_dmem(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon,
	const uint8_t *data,
	uint32_t length,
	uint32_t address)
{
	uint32_t at;

	/* Refuses a length that is not whole words. */
	if (length == 0U || (length % 4U) != 0U)
		return EINVAL;

	/* Opens the port at the address, incrementing, and writes the words. */
	nvrtx_wr32(gpu, falcon->base + FALCON_DMEM_CONTROL, FALCON_PORT_AUTO_INCREMENT | address);
	for (at = 0U; at < length; at += 4U)
		nvrtx_wr32(gpu, falcon->base + FALCON_DMEM_DATA, nvrtx_get32(data, at));

	/* Succeeded: the data is in the data memory. */
	return 0;
}

/*
 * Starts a loaded falcon and waits for it to halt.
 *
 * The mailboxes are written before the start and read after the halt.
 * Returns ETIMEDOUT when the falcon did not halt within 2 s; the mailboxes
 * are read either way.
 */
int
nvrtx_falcon_boot(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon,
	uint32_t boot_address,
	uint32_t *mailbox0,
	uint32_t *mailbox1)
{
	int error;

	/* Hands the program its two arguments. */
	nvrtx_wr32(gpu, falcon->base + FALCON_MAILBOX0, *mailbox0);
	nvrtx_wr32(gpu, falcon->base + FALCON_MAILBOX1, *mailbox1);

	/* Sets the start address and starts the CPU. */
	nvrtx_wr32(gpu, falcon->base + FALCON_BOOT_VECTOR, boot_address);
	nvrtx_wr32(gpu, falcon->base + FALCON_CPU_CONTROL, FALCON_CPU_START);

	/* Waits for the CPU to halt. */
	error = nvrtx_poll(gpu, falcon->base + FALCON_CPU_CONTROL, FALCON_CPU_HALTED, FALCON_CPU_HALTED, FALCON_HALT_TIMEOUT_US);

	/* Reads what the program left in the mailboxes. */
	*mailbox0 = nvrtx_rd32(gpu, falcon->base + FALCON_MAILBOX0);
	*mailbox1 = nvrtx_rd32(gpu, falcon->base + FALCON_MAILBOX1);

	/* Reports a falcon that did not halt. */
	if (error != 0)
		return error;

	/* Succeeded: the program ran to its halt. */
	return 0;
}

/*
 * Pulses a falcon's engine reset and waits for the scrub.
 *
 * This alone is how the GSP's falcon is put into its RISC-V mode before the booter starts it.
 */
int
nvrtx_falcon_reset_engine(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon)
{
	int error;

	/* Holds the engine in reset for 10 us, then lets it go. */
	falcon_mask(gpu, falcon->base + FALCON_ENGINE_RESET, 0x1U, 0x1U);
	kern_usleep_range(10U, 20U);
	falcon_mask(gpu, falcon->base + FALCON_ENGINE_RESET, 0x1U, 0U);

	/* Waits for the memories to be scrubbed. */
	error = falcon_scrub_wait(gpu, falcon);
	if (error != 0)
		return error;

	/* Succeeded: the engine is out of reset. */
	return 0;
}

/* Changes the masked bits of a register to a value. */
static void
falcon_mask(
	struct nvrtx_gpu *gpu,
	uint32_t offset,
	uint32_t mask,
	uint32_t value)
{
	uint32_t current;

	/* Reads, replaces the masked bits, and writes back. */
	current = nvrtx_rd32(gpu, offset);
	nvrtx_wr32(gpu, offset, (current & ~mask) | value);
}

/* Waits for a falcon's memories to finish scrubbing after a reset. */
static int
falcon_scrub_wait(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_falcon *falcon)
{
	int error;

	/* Touches the scrub control, then waits for both scrubbing bits to clear. */
	falcon_mask(gpu, falcon->base + FALCON_SCRUB_TOUCH, 0U, 0U);
	error = nvrtx_poll(gpu, falcon->base + FALCON_DMA_CONTROL, FALCON_SCRUBBING, 0U, FALCON_SCRUB_TIMEOUT_US);
	if (error != 0)
		return error;

	/* Succeeded: the memories are clean. */
	return 0;
}
