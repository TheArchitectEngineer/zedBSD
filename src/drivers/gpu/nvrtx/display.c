/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display under the GSP (stage P8a: bring-up and readout).
 *
 * The resource manager owns the display hardware's state machine; the
 * driver gives it the display's instance memory (64 KiB of VRAM), makes the
 * display's common object and the Turing display root, and asks which heads
 * the firmware's picture (the GOP) lit, on which output (SOR) and protocol.
 * The timing of a lit head is read from the display's armed state, which the
 * kernel can read directly.  Nothing here changes what is on the screen;
 * the core and window channels that take the head over and flip are stage
 * P8b (scanout.c).
 * The order and the controls are nouveau's (r535 display bring-up, with the
 * 570 numbers of the system controls).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The display's instance memory: 64 KiB of VRAM, 1 MiB below the first block. */
#define DISPLAY_INSTANCE_BELOW		0x100000ULL
#define DISPLAY_INSTANCE_BYTES		0x10000U
#define DISPLAY_SPACE_VRAM		2U
#define DISPLAY_CACHE_WRITE_COMBINED	2U

/* The controls of the bring-up and the readout. */
#define DISPLAY_CTL_WRITE_INSTANCE	0x20800a49U
#define DISPLAY_CTL_STATIC_INFO		0x20800a01U
#define DISPLAY_CTL_BRIGHTNESS_LOAD	0x20800ac6U
#define DISPLAY_CTL_MANUAL_DP		0x00731365U
#define DISPLAY_CTL_HEAD_COUNT		0x00730102U
#define DISPLAY_CTL_HEAD_MASK		0x00730287U
#define DISPLAY_CTL_SUPPORTED		0x00730107U
#define DISPLAY_CTL_ACTIVE		0x0073010cU
#define DISPLAY_CTL_OUTPUT_INFO		0x0073028bU

/* Their parameter lengths. */
#define DISPLAY_WRITE_INSTANCE_BYTES	24U
#define DISPLAY_STATIC_INFO_BYTES	36U
#define DISPLAY_BRIGHTNESS_LOAD_BYTES	4104U
#define DISPLAY_MANUAL_DP_BYTES		4U
#define DISPLAY_HEAD_COUNT_BYTES	12U
#define DISPLAY_HEAD_MASK_BYTES		8U
#define DISPLAY_SUPPORTED_BYTES		12U
#define DISPLAY_ACTIVE_BYTES		16U
#define DISPLAY_OUTPUT_INFO_BYTES	56U

/* The fields the driver reads or writes. */
#define DISPLAY_STATIC_WINDOWS_AT	4U
#define DISPLAY_HEAD_COUNT_AT		8U
#define DISPLAY_HEAD_MASK_AT		4U
#define DISPLAY_SUPPORTED_MASK_AT	4U
#define DISPLAY_ACTIVE_HEAD_AT		4U
#define DISPLAY_ACTIVE_ID_AT		12U
#define DISPLAY_OUTPUT_ID_AT		4U
#define DISPLAY_OUTPUT_INDEX_AT		8U
#define DISPLAY_OUTPUT_TYPE_AT		12U
#define DISPLAY_OUTPUT_PROTOCOL_AT	16U

/* "Not supported": the brightness state the driver has no ACPI method to load. */
#define DISPLAY_STATUS_NOT_SUPPORTED	0x56U

/* The objects: the common object and the Turing display root. */
#define DISPLAY_CLASS_COMMON		0x0073U
#define DISPLAY_CLASS_ROOT		0xc570U
#define DISPLAY_HANDLE_COMMON		0x00730000U
#define DISPLAY_HANDLE_ROOT		0xc5700000U

/* The most heads the readout looks at. */
#define DISPLAY_HEAD_MAX		8U

/* A head's armed state: the raster, sync and blank words, and the pixel clock. */
#define DISPLAY_REG_ARMED_HEAD		0x68a000U
#define DISPLAY_HEAD_STRIDE		0x400U
#define DISPLAY_ARMED_CONTROL_AT	0x004U
#define DISPLAY_ARMED_CLOCK_AT		0x00cU
#define DISPLAY_ARMED_TOTAL_AT		0x064U
#define DISPLAY_ARMED_SYNC_END_AT	0x068U
#define DISPLAY_ARMED_BLANK_END_AT	0x06cU
#define DISPLAY_ARMED_BLANK_START_AT	0x070U

/* The output kinds and protocols the readout names. */
#define DISPLAY_TYPE_SOR		2U
#define DISPLAY_PROTOCOL_TMDS_A		1U
#define DISPLAY_PROTOCOL_TMDS_B		2U
#define DISPLAY_PROTOCOL_TMDS_DUAL	5U
#define DISPLAY_PROTOCOL_DP_A		8U
#define DISPLAY_PROTOCOL_DP_B		9U

static int display_control(struct nvrtx_gpu *gpu, uint32_t client, uint32_t object, uint32_t command, uint8_t *params, size_t length);
static int display_instance(struct nvrtx_gpu *gpu);
static int display_counts(struct nvrtx_gpu *gpu);
static void display_readout(struct nvrtx_gpu *gpu);
static void display_head(struct nvrtx_gpu *gpu, uint32_t head, uint32_t display_id);
static const char *display_protocol_name(uint32_t protocol);

/*
 * Runs stage P8a: the display's instance memory, its objects and the readout of the lit heads.
 *
 * A failure is returned; the finish's stop frees the objects that were made.
 */
int
nvrtx_display_start(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* Gives the resource manager the display's instance memory. */
	nvrtx_stage_mark("P8 begin");
	error = display_instance(gpu);
	if (error != 0)
		return error;

	/* Makes the common object the system controls go to. */
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_DEVICE, DISPLAY_HANDLE_COMMON, DISPLAY_CLASS_COMMON, NULL, 0U);
	if (error != 0)
		return error;

	display->common_made = 1;

	/* Reads the windows and heads, and declares DisplayPort handled by hand (as nouveau does). */
	error = display_counts(gpu);
	if (error != 0)
		return error;

	/* Makes the Turing display root. */
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_DEVICE, DISPLAY_HANDLE_ROOT, DISPLAY_CLASS_ROOT, NULL, 0U);
	if (error != 0)
		return error;

	display->root_made = 1;

	/* Says which heads the firmware lit, and how. */
	display_readout(gpu);

	/* Succeeded: the display is the driver's to drive from here. */
	nvrtx_stage_mark("P8 ok: display, heads %x, windows %x, lit %x",
	    (unsigned)display->head_mask,
	    (unsigned)display->window_mask,
	    (unsigned)display->lit_mask);
	return 0;
}

/*
 * Frees the display's objects before the unload; failures are only shown.
 */
void
nvrtx_display_stop(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	int failures;
	int error;

	display = &gpu->display;
	failures = 0;

	/* Frees P8b's channels first: they are children of the root. */
	nvrtx_scanout_stop(gpu);

	/* Frees the root, then the common object. */
	if (display->root_made != 0) {
		error = nvrtx_rm_free(gpu, DISPLAY_HANDLE_ROOT);
		if (error != 0)
			failures++;
		display->root_made = 0;
	}

	if (display->common_made != 0) {
		error = nvrtx_rm_free(gpu, DISPLAY_HANDLE_COMMON);
		if (error != 0)
			failures++;
		display->common_made = 0;
	}

	/* Says how the stop went, when anything failed. */
	if (failures != 0)
		nvrtx_stage_mark("P6: %d display frees failed; the unload removes the rest", failures);
}

/* Sends a control whose reply the caller reads in place: the reply is copied back over the parameters. */
static int
display_control(
	struct nvrtx_gpu *gpu,
	uint32_t client,
	uint32_t object,
	uint32_t command,
	uint8_t *params,
	size_t length)
{
	const uint8_t *reply;
	size_t reply_length;
	int error;

	/* Sends it and waits for the reply. */
	error = nvrtx_rm_control(gpu, client, object, command, params, length, &reply, &reply_length);
	if (error != 0)
		return error;

	/* A short reply is not this layout. */
	if (reply_length < length)
		return EIO;

	/* Succeeded: the reply is in the parameters. */
	kern_memcpy(params, reply, length);
	return 0;
}

/* Clears the display's instance memory and hands it to the resource manager. */
static int
display_instance(
	struct nvrtx_gpu *gpu)
{
	uint8_t params[DISPLAY_WRITE_INSTANCE_BYTES];
	int error;

	/* Takes 64 KiB 1 MiB below the first block (between it and the graphics arena) and clears it. */
	gpu->display.instance = gpu->engine.vram_block - DISPLAY_INSTANCE_BELOW;
	error = nvrtx_vram_zero(gpu, gpu->display.instance, DISPLAY_INSTANCE_BYTES);
	if (error != 0)
		return error;

	/* Names it: its address, size, the VRAM and write-combined. */
	kern_memset(params, 0, sizeof(params));
	nvrtx_put64(params, 0U, gpu->display.instance);
	nvrtx_put64(params, 8U, DISPLAY_INSTANCE_BYTES);
	nvrtx_put32(params, 16U, DISPLAY_SPACE_VRAM);
	nvrtx_put32(params, 20U, DISPLAY_CACHE_WRITE_COMBINED);
	error = display_control(gpu, gpu->internal_client, gpu->internal_subdevice, DISPLAY_CTL_WRITE_INSTANCE, params, sizeof(params));
	if (error != 0)
		return error;

	/* Succeeded: the resource manager uses the memory. */
	return 0;
}

/*
 * Reads the windows and heads, loads the brightness state and takes DisplayPort by hand.
 *
 * The brightness state is "not supported" (the driver evaluates no ACPI
 * method for it), as nouveau sends when the method is absent.
 */
static int
display_counts(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint8_t params[DISPLAY_HEAD_COUNT_BYTES + DISPLAY_STATIC_INFO_BYTES];
	uint8_t *brightness;
	int error;

	display = &gpu->display;

	/* The windows the hardware has. */
	kern_memset(params, 0, sizeof(params));
	error = display_control(gpu, gpu->internal_client, gpu->internal_subdevice, DISPLAY_CTL_STATIC_INFO, params, DISPLAY_STATIC_INFO_BYTES);
	if (error != 0)
		return error;

	display->window_mask = nvrtx_get32(params, DISPLAY_STATIC_WINDOWS_AT);

	/* The brightness state. */
	brightness = kern_calloc(1U, DISPLAY_BRIGHTNESS_LOAD_BYTES);
	if (brightness == NULL)
		return ENOMEM;

	nvrtx_put32(brightness, 0U, DISPLAY_STATUS_NOT_SUPPORTED);
	error = display_control(gpu, gpu->internal_client, gpu->internal_subdevice, DISPLAY_CTL_BRIGHTNESS_LOAD, brightness, DISPLAY_BRIGHTNESS_LOAD_BYTES);
	kern_free(brightness);
	if (error != 0)
		return error;

	/* DisplayPort is handled by the driver's calls, not by the resource manager on its own. */
	kern_memset(params, 0, sizeof(params));
	error = display_control(gpu, NVRTX_HANDLE_CLIENT, DISPLAY_HANDLE_COMMON, DISPLAY_CTL_MANUAL_DP, params, DISPLAY_MANUAL_DP_BYTES);
	if (error != 0)
		return error;

	/* The number of heads, and which exist. */
	kern_memset(params, 0, sizeof(params));
	error = display_control(gpu, NVRTX_HANDLE_CLIENT, DISPLAY_HANDLE_COMMON, DISPLAY_CTL_HEAD_COUNT, params, DISPLAY_HEAD_COUNT_BYTES);
	if (error != 0)
		return error;

	display->head_count = nvrtx_get32(params, DISPLAY_HEAD_COUNT_AT);
	kern_memset(params, 0, sizeof(params));
	error = display_control(gpu, NVRTX_HANDLE_CLIENT, DISPLAY_HANDLE_COMMON, DISPLAY_CTL_HEAD_MASK, params, DISPLAY_HEAD_MASK_BYTES);
	if (error != 0)
		return error;

	display->head_mask = nvrtx_get32(params, DISPLAY_HEAD_MASK_AT);

	/* The outputs (display ids) the board has. */
	kern_memset(params, 0, sizeof(params));
	error = display_control(gpu, NVRTX_HANDLE_CLIENT, DISPLAY_HANDLE_COMMON, DISPLAY_CTL_SUPPORTED, params, DISPLAY_SUPPORTED_BYTES);
	if (error != 0)
		return error;

	/* Succeeded: the display's shape is known. */
	display->output_mask = nvrtx_get32(params, DISPLAY_SUPPORTED_MASK_AT);
	nvrtx_stage_mark("P8 heads %u (%x), windows %x, outputs %x",
	    (unsigned)display->head_count,
	    (unsigned)display->head_mask,
	    (unsigned)display->window_mask,
	    (unsigned)display->output_mask);
	return 0;
}

/* Asks, for every head, which output the firmware lit on it, and reads its timing. */
static void
display_readout(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint8_t params[DISPLAY_ACTIVE_BYTES];
	uint32_t head;
	uint32_t display_id;
	int error;

	display = &gpu->display;

	/* Asks for each head that exists. */
	for (head = 0U; head < DISPLAY_HEAD_MAX; head++) {
		if ((display->head_mask & (1U << head)) == 0U)
			continue;

		kern_memset(params, 0, sizeof(params));
		nvrtx_put32(params, DISPLAY_ACTIVE_HEAD_AT, head);
		error = display_control(gpu, NVRTX_HANDLE_CLIENT, DISPLAY_HANDLE_COMMON, DISPLAY_CTL_ACTIVE, params, sizeof(params));
		if (error != 0) {
			nvrtx_stage_mark("P8 head %u: active query failed (%d)", (unsigned)head, error);
			continue;
		}

		/* A head with no output is dark. */
		display_id = nvrtx_get32(params, DISPLAY_ACTIVE_ID_AT);
		if (display_id == 0U)
			continue;

		display->lit_mask |= 1U << head;
		display_head(gpu, head, display_id);
	}
}

/* Shows one lit head: its output (SOR and protocol) and the timing it was armed with. */
static void
display_head(
	struct nvrtx_gpu *gpu,
	uint32_t head,
	uint32_t display_id)
{
	uint8_t params[DISPLAY_OUTPUT_INFO_BYTES];
	uint32_t base;
	uint32_t total;
	uint32_t blank_end;
	uint32_t blank_start;
	uint32_t clock;
	uint32_t output;
	uint32_t type;
	uint32_t protocol;
	int error;

	/* Asks which output resource drives the display. */
	kern_memset(params, 0, sizeof(params));
	nvrtx_put32(params, DISPLAY_OUTPUT_ID_AT, display_id);
	error = display_control(gpu, NVRTX_HANDLE_CLIENT, DISPLAY_HANDLE_COMMON, DISPLAY_CTL_OUTPUT_INFO, params, sizeof(params));
	output = 0xffffffffU;
	type = 0U;
	protocol = 0xffffffffU;
	if (error == 0) {
		output = nvrtx_get32(params, DISPLAY_OUTPUT_INDEX_AT);
		type = nvrtx_get32(params, DISPLAY_OUTPUT_TYPE_AT);
		protocol = nvrtx_get32(params, DISPLAY_OUTPUT_PROTOCOL_AT);
	}

	/* Keeps the first lit head for P8b. */
	if (gpu->display.gop_head_found == 0) {
		gpu->display.gop_head_found = 1;
		gpu->display.gop_head = head;
		gpu->display.gop_display_id = display_id;
		gpu->display.gop_sor = output;
		gpu->display.gop_protocol = protocol;
	}

	/* Reads the armed raster: active size is blank start less blank end. */
	base = DISPLAY_REG_ARMED_HEAD + head * DISPLAY_HEAD_STRIDE;
	total = nvrtx_rd32(gpu, base + DISPLAY_ARMED_TOTAL_AT);
	blank_end = nvrtx_rd32(gpu, base + DISPLAY_ARMED_BLANK_END_AT);
	blank_start = nvrtx_rd32(gpu, base + DISPLAY_ARMED_BLANK_START_AT);
	clock = nvrtx_rd32(gpu, base + DISPLAY_ARMED_CLOCK_AT);
	nvrtx_stage_mark("P8 head %u: display %x %s %u %s", (unsigned)head, (unsigned)display_id,
	    type == DISPLAY_TYPE_SOR ? "SOR" : "OR",
	    (unsigned)output,
	    display_protocol_name(protocol));
	nvrtx_stage_mark("P8 head %u: %ux%u of %ux%u, clock %u",
	    (unsigned)head,
	    (unsigned)((blank_start & 0xffffU) - (blank_end & 0xffffU)),
	    (unsigned)((blank_start >> 16) - (blank_end >> 16)),
	    (unsigned)(total & 0xffffU),
	    (unsigned)(total >> 16),
	    (unsigned)clock);
}

/* Names an output protocol for the marks. */
static const char *
display_protocol_name(
	uint32_t protocol)
{
	/* The protocols a Turing board's connectors use. */
	switch (protocol) {
	case DISPLAY_PROTOCOL_TMDS_A:
		return "TMDS-A";
	case DISPLAY_PROTOCOL_TMDS_B:
		return "TMDS-B";
	case DISPLAY_PROTOCOL_TMDS_DUAL:
		return "TMDS-dual";
	case DISPLAY_PROTOCOL_DP_A:
		return "DP-A";
	case DISPLAY_PROTOCOL_DP_B:
		return "DP-B";
	default:
		break;
	}

	/* Anything else is shown by number elsewhere. */
	return "other";
}
