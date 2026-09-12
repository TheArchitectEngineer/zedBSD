/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks real native timing discovery, malformed metadata fallback and nominal FIFO cadence.
 * The strict protocol peer supplies independent base/CTA detailed timing descriptors.
 */

#define VENUS_CONSOLE_ENTRY venus_console_unused_main
#include "../../ws030/tests/venus-console.c"
#undef VENUS_CONSOLE_ENTRY

static void edid_fixture(void);
static void edid_checksum(uint8_t *block);
static void edid_descriptor(uint8_t *descriptor, uint32_t width, uint32_t height, uint32_t blank_width, uint32_t blank_height, uint32_t clock);
static void edid_discovery(void);
static void edid_virtual_present(void);
static void edid_cadence(void);

/*
 * Exercises native metadata and guest pacing through the production display implementation.
 */
int
main(void)
{
	/* Discovery must reflect checked device metadata, including real additional frequencies. */
	edid_discovery();

	/* A discovered frequency remains usable for custom virtual geometry in the direct display path. */
	edid_virtual_present();

	/* Fractional nominal periods may not all collapse to the slower two-tick cadence. */
	edid_cadence();
	assert(fixture_allocations == 0U);
	assert(fixture_dma == 0U);
	assert(fixture_mappings == 0U);
	puts("Venus EDID: GET_EDID base/CTA DTD, checksums, extents, nominal refresh, duplicate/high-rate rejection, fallback, custom 320x240 direct scanout at 60/74.994Hz and fractional guest pacing PASS");

	/* Succeeded: native mode metadata and virtual timing have independent verified boundaries. */
	return 0;
}

/* Builds an independent base/CTA EDID with three supported modes and deliberate duplicates. */
static void
edid_fixture(void)
{
	uint32_t index;

	/* Base EDID has a valid signature and reports its actual physical dimensions. */
	memset(console_edid, 0, sizeof(console_edid));
	for (index = 1U; index < 7U; index++)
		console_edid[index] = 255U;
	console_edid[18] = 1U;
	console_edid[19] = 4U;
	console_edid[21] = 52U;
	console_edid[22] = 29U;
	console_edid[126] = 1U;

	/* Independent pixel clocks and complete blanking yield exactly 60Hz and 64Hz. */
	edid_descriptor(console_edid + 54U, 640U, 480U, 160U, 45U, 2520U);
	edid_descriptor(console_edid + 72U, 800U, 600U, 200U, 25U, 4000U);
	edid_descriptor(console_edid + 90U, 640U, 480U, 160U, 45U, 2520U);

	/* A nominal 200Hz mode exceeds the actual 100Hz guest FIFO clock and must not be exposed. */
	edid_descriptor(console_edid + 108U, 320U, 240U, 80U, 10U, 2000U);
	edid_checksum(console_edid);

	/* The CTA detailed region adds 1280x720 at a independently specified 74.25MHz pixel clock. */
	console_edid[128U] = 2U;
	console_edid[129U] = 3U;
	console_edid[130U] = 4U;
	edid_descriptor(console_edid + 132U, 1280U, 720U, 370U, 30U, 7425U);
	edid_checksum(console_edid + 128U);
	console_edid_bytes = 256U;

	/* Succeeded: no expected public mode was obtained by calling the production parser. */
	return;
}

/* Supplies a checksum whose full-block sum is zero modulo 256. */
static void
edid_checksum(
	uint8_t *block)
{
	uint32_t sum;
	uint32_t index;

	/* The checksum includes every header, payload and extension-count byte. */
	sum = 0U;
	for (index = 0U; index < 127U; index++)
		sum += block[index];
	block[127] = (uint8_t)(0U - sum);

	/* Succeeded: changing any protected byte later produces a malformed EDID. */
	return;
}

/* Encodes a standard eighteen-byte progressive detailed timing descriptor. */
static void
edid_descriptor(
	uint8_t *descriptor,
	uint32_t width,
	uint32_t height,
	uint32_t blank_width,
	uint32_t blank_height,
	uint32_t clock)
{
	/* Pixel clock units are ten kilohertz as required by the EDID wire format. */
	memset(descriptor, 0, 18U);
	descriptor[0] = (uint8_t)clock;
	descriptor[1] = (uint8_t)(clock >> 8);

	/* Active and blanking horizontal extents have independent twelve-bit values. */
	descriptor[2] = (uint8_t)width;
	descriptor[3] = (uint8_t)blank_width;
	descriptor[4] = (uint8_t)(((width >> 8) << 4) | (blank_width >> 8));

	/* Vertical extents use the matching separate high-bit nibbles. */
	descriptor[5] = (uint8_t)height;
	descriptor[6] = (uint8_t)blank_height;
	descriptor[7] = (uint8_t)(((height >> 8) << 4) | (blank_height >> 8));

	/* Succeeded: the peer carries real timing arithmetic without using driver helpers. */
	return;
}

/* Queries real protocol responses and validates only their supported nominal timing domain. */
static void
edid_discovery(void)
{
	struct venus_controller controller;
	struct gpu_display_info information;
	struct gpu_display_mode mode;
	uint8_t configuration[16];
	unsigned before;
	int error;

	/* Optional feature negotiation is the only reason the display engine may issue GET_EDID. */
	fixture_controller(&controller, configuration);
	controller.transport.features |= VENUS_FEATURE_EDID;
	edid_fixture();
	memset(&information, 0, sizeof(information));
	error = display_query(&controller, NULL, &information);
	assert(error == 0);
	assert(fixture_commands[0x10aU] == 1U);
	assert(information.preferred_width == 640U);
	assert(information.preferred_height == 480U);
	assert(information.refresh_millihz == 60000U);
	assert(information.physical_width_mm == 520U);
	assert(information.physical_height_mm == 290U);

	/* Preferred, additional base DTD and CTA DTD are enumerated exactly once. */
	memset(&mode, 0, sizeof(mode));
	mode.display_id = information.display_id;
	mode.generation = information.generation;
	mode.operation = GPU_DISPLAY_MODE_ENUMERATE;
	mode.index = GPU_DISPLAY_COUNT_ONLY;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.count == 3U);
	mode.index = 0U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.width == 640U && mode.height == 480U && mode.refresh_millihz == 60000U);
	mode.index = 1U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.width == 800U && mode.height == 600U && mode.refresh_millihz == 64000U);
	mode.index = 2U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.width == 1280U && mode.height == 720U && mode.refresh_millihz == 60000U);
	mode.index = 3U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == EINVAL);

	/* Automatic selection uses native timing, while explicit virtual cadence also permits custom geometry. */
	mode.operation = GPU_DISPLAY_MODE_VALIDATE;
	mode.index = 0U;
	mode.width = 800U;
	mode.height = 600U;
	mode.refresh_millihz = 0U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.refresh_millihz == 64000U);
	mode.width = 320U;
	mode.height = 240U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.refresh_millihz == 64000U);
	mode.refresh_millihz = 200000U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == EINVAL);

	/* Existing 320x240 custom virtual presentation remains valid at its defined 50Hz fallback. */
	mode.refresh_millihz = 0U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.refresh_millihz == 50000U);

	/* Corrupt optional EDID must withdraw stale timings and fall back to the actual display rectangle. */
	before = fixture_allocations;
	console_edid[21]++;
	drv_venus_store32(configuration, 1U);
	error = display_refresh(&controller);
	assert(error == 0);
	drv_venus_store32(configuration, 0U);
	memset(&information, 0, sizeof(information));
	error = display_query(&controller, NULL, &information);
	assert(error == 0);
	assert(information.refresh_millihz == 50000U);
	assert(information.preferred_width == 640U);
	assert(information.physical_width_mm == 0U);
	assert(controller.display->outputs[0].mode_count == 1U);
	assert(fixture_allocations + 3U == before);

	/* A valid checksum cannot authorize an extension beyond the host-reported payload size. */
	edid_fixture();
	console_edid_bytes = 128U;
	drv_venus_store32(configuration, 1U);
	error = display_refresh(&controller);
	assert(error == 0);
	assert(controller.display->outputs[0].timings == NULL);

	/* Malformed CTA timing offsets reject the complete optional inventory and all partial records. */
	edid_fixture();
	console_edid[130] = 2U;
	edid_checksum(console_edid + 128U);
	error = display_refresh(&controller);
	assert(error == 0);
	assert(controller.display->outputs[0].timings == NULL);

	/* A host without the EDID feature must not receive an unsupported optional command. */
	before = fixture_commands[0x10aU];
	controller.transport.features &= ~VENUS_FEATURE_EDID;
	error = display_refresh(&controller);
	assert(error == 0);
	assert(fixture_commands[0x10aU] == before);
	assert(controller.display->outputs[0].preferred_refresh == 50000U);
	drv_venus_display_finish(&controller);

	/* Succeeded: topology metadata did not leak any output or timing ownership. */
	return;
}

/* Exercises a custom direct scanout using native and independently specified virtual cadences. */
static void
edid_virtual_present(void)
{
	struct venus_controller controller;
	struct gpu_display_info information;
	struct gpu_display_mode mode;
	struct gpu_display_claim claim;
	struct gpu_display_present present;
	struct gpu_resource_create allocation;
	struct venus_resource *front;
	uint8_t configuration[16];
	uint64_t previous;
	void *session;
	void *storage;
	unsigned scanouts;
	int error;

	/* The first advertised timing is 640x480 at 60Hz, independently supplied by the EDID peer. */
	fixture_controller(&controller, configuration);
	controller.transport.features |= VENUS_FEATURE_EDID;
	edid_fixture();
	memset(&information, 0, sizeof(information));
	error = display_query(&controller, NULL, &information);
	assert(error == 0);

	/* Copy the same first enumerated mode that vkdemo uses before replacing its geometry. */
	memset(&mode, 0, sizeof(mode));
	mode.display_id = information.display_id;
	mode.generation = information.generation;
	mode.operation = GPU_DISPLAY_MODE_ENUMERATE;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.width == 640U);
	assert(mode.height == 480U);
	assert(mode.refresh_millihz == 60000U);

	/* A custom framebuffer rectangle is not a physical monitor timing change. */
	mode.operation = GPU_DISPLAY_MODE_VALIDATE;
	mode.count = 0U;
	mode.width = 320U;
	mode.height = 240U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.refresh_millihz == 60000U);

	/* The direct display path owns a normal session and storage independently of its scanout buffers. */
	error = venus_open(&controller, &session);
	assert(error == 0);
	console_test_claim(&controller, session, &claim);

	/* Allocate the whole image without using the GPU sharing or blob presentation path. */
	memset(&allocation, 0, sizeof(allocation));
	allocation.bytes = 320U * 240U * 4U;
	error = venus_resource_create(&controller, session, &allocation, &storage);
	assert(error == 0);

	/* Native selection must accept the enumerated frequency with the caller's smaller framebuffer. */
	memset(&present, 0, sizeof(present));
	present.lease = claim.lease;
	present.generation = claim.generation;
	present.width = mode.width;
	present.height = mode.height;
	present.stride = mode.width * 4U;
	present.format = GPU_PIXEL_BGRA8888;
	present.refresh_millihz = mode.refresh_millihz;
	present.flags = GPU_DISPLAY_PRESENT_FIFO;
	scanouts = fixture_commands[0x103U];
	previous = console_ticks;
	error = display_present(&controller, session, storage, &present);
	assert(error == 0);
	assert(present.sequence == 1U);
	assert(console_ticks > previous);
	assert(fixture_commands[0x103U] == scanouts + 1U);
	front = controller.display->outputs[0].front;
	assert(front != NULL);
	assert(fixture_scanout == front->identifier);
	assert(front->width == 320U);
	assert(front->height == 240U);

	/* The measured QEMU EDID rate also works as an explicit custom cadence without a matching DTD. */
	mode.refresh_millihz = 74994U;
	error = display_mode(&controller, NULL, &mode);
	assert(error == 0);
	assert(mode.refresh_millihz == 74994U);
	present.refresh_millihz = mode.refresh_millihz;
	present.sequence = 0U;
	previous = console_ticks;
	error = display_present(&controller, session, storage, &present);
	assert(error == 0);
	assert(present.sequence == 2U);
	assert(console_ticks > previous);
	assert(fixture_commands[0x103U] == scanouts + 2U);
	assert(controller.primary_width == 320U);
	assert(controller.primary_height == 240U);

	/* Cadences below one hertz or above the guest tick rate cannot submit a false native selection. */
	present.sequence = 0U;
	present.refresh_millihz = 999U;
	error = display_present(&controller, session, storage, &present);
	assert(error == EINVAL);
	present.refresh_millihz = 100001U;
	error = display_present(&controller, session, storage, &present);
	assert(error == EINVAL);
	assert(fixture_commands[0x103U] == scanouts + 2U);

	/* Closing the source session releases the direct scanout before the controller stops. */
	venus_resource_destroy(&controller, session, storage);
	venus_close(&controller, session);
	error = drv_venus_display_stop(&controller);
	assert(error == 0);
	console_test_finish(&controller);

	/* Succeeded: custom direct presentation and its failure bounds leave no resource or worker owner. */
	return;
}

/* Requires distinct guest ticks and the correct average fractional nominal frequency. */
static void
edid_cadence(void)
{
	struct venus_display_output output;
	uint64_t previous;
	uint64_t step;
	uint32_t frames;
	unsigned short_periods;
	unsigned long_periods;
	int error;

	/* Sixty nominal frames must occupy one hundred 10ms ticks, not one hundred twenty. */
	memset(&output, 0, sizeof(output));
	console_ticks = 0U;
	short_periods = 0U;
	long_periods = 0U;
	for (frames = 0U; frames < 60U; frames++) {
		previous = console_ticks;
		error = display_next_refresh(&output, 60000U);
		assert(error == 0);
		step = console_ticks - previous;
		assert(step == 1U || step == 2U);
		if (step == 1U)
			short_periods++;
		if (step == 2U)
			long_periods++;
		output.present_tick = console_ticks;
	}
	assert(console_ticks == 100U);
	assert(short_periods == 20U);
	assert(long_periods == 40U);

	/* The exact 50Hz compatibility cadence remains two ticks per completed frame. */
	memset(&output, 0, sizeof(output));
	console_ticks = 0U;
	for (frames = 0U; frames < 50U; frames++) {
		error = display_next_refresh(&output, 50000U);
		assert(error == 0);
		assert(console_ticks == (uint64_t)(frames + 1U) * 2U);
		output.present_tick = console_ticks;
	}

	/* No unsupported high frequency or arithmetic overflow can produce an immediate false completion. */
	error = display_next_refresh(&output, 100001U);
	assert(error == EINVAL);
	console_ticks = UINT64_MAX;
	error = display_next_refresh(&output, 60000U);
	assert(error == EOVERFLOW);

	/* Succeeded: cadence retains nominal phase while acknowledging the guest clock resolution. */
	return;
}
