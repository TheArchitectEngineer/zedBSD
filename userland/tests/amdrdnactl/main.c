/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The amdrdna control tool: reads the driver's state, asks for further
 * bring-up stages and saves the GPU's video BIOS image, through the
 * driver's control device.
 *
 *   amdrdnactl [-f DEVICE] [status]       prints the state, one "key=value" per line
 *   amdrdnactl [-f DEVICE] advance STAGE  asks for the stages up to STAGE (root)
 *   amdrdnactl [-f DEVICE] vbios FILE     writes the video BIOS image to FILE
 *   amdrdnactl [-f DEVICE] display        prints the display the driver took
 *                                         over: its registers and counts
 *   amdrdnactl [-f DEVICE] screen FILE    writes the picture the display shows
 *                                         to FILE as a PPM image (root)
 *   amdrdnactl gpu [GPU-DEVICE]           checks the GPU device the driver
 *                                         registered (/dev/gpu0 by default)
 *
 * The exit status is 0, 1 with "amdrdnactl: <step>: <reason>" on a failure,
 * or 2 for a wrong command line.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/amdrdna.h>

#include "amdrdnactl.h"

/* The default devices: the first GPU's control device and its GPU device. */
#define CONTROL_DEVICE		"/dev/amdrdna0"
#define CONTROL_GPU_DEVICE	"/dev/gpu0"

/* The bytes one request of the image copies. */
#define CONTROL_CHUNK		4096U

/* The names of the stages, by number. */
static const char *const stage_names[AMDRDNA_STAGE_COUNT] = {
	"none", "A0", "A1", "A1v", "A2", "A3t", "A3", "A4", "A5", "A5s",
	"A6", "A7", "A8", "A9", "A5c", "A10", "A11", "A12", "A13"
};

static int control_fail(const char *step, int error);
static int control_status(int descriptor);
static int control_advance(int descriptor, const char *stage);
static int control_vbios(int descriptor, const char *path);
static int control_display(int descriptor);
static int control_screen(int descriptor, const char *path);
static const char *stage_name(uint32_t stage);
static int stage_number(const char *name, uint32_t *stage);
static void usage(void);

/*
 * Runs one command on the control device.
 */
int
main(
	int argc,
	char **argv)
{
	const char *path;
	const char *command;
	int descriptor;
	int first;
	int result;
	int named;
	int is_status;
	int is_advance;
	int is_vbios;
	int is_display;
	int is_screen;
	int is_gpu;
	int is_modes;

	/* The GPU device check and the modes need no control device. */
	is_gpu = 0;
	is_modes = 0;
	if (argc >= 2) {
		is_gpu = strcmp(argv[1], "gpu") == 0;
		is_modes = strcmp(argv[1], "modes") == 0;
	}
	if (is_gpu || is_modes) {
		path = CONTROL_GPU_DEVICE;
		if (argc == 3)
			path = argv[2];
		if (is_modes)
			result = control_modes(path);
		else
			result = control_gpu(path);
		return result;
	}

	/* The device: -f names another one. */
	path = CONTROL_DEVICE;
	first = 1;
	named = 0;
	if (argc >= 3)
		named = strcmp(argv[1], "-f") == 0;
	if (named) {
		path = argv[2];
		first = 3;
	}

	/* The command, status by default, and which one it is. */
	command = "status";
	if (first < argc)
		command = argv[first];
	is_status = strcmp(command, "status") == 0;
	is_advance = strcmp(command, "advance") == 0;
	is_vbios = strcmp(command, "vbios") == 0;
	is_display = strcmp(command, "display") == 0;
	is_screen = strcmp(command, "screen") == 0;

	/* Opens the device. */
	descriptor = open(path, O_RDONLY);
	if (descriptor < 0)
		return control_fail(path, errno);

	/* Runs the command with the words it takes. */
	if (is_status && first + 1 >= argc) {
		result = control_status(descriptor);
	} else if (is_advance && first + 2 == argc) {
		result = control_advance(descriptor, argv[first + 1]);
	} else if (is_vbios && first + 2 == argc) {
		result = control_vbios(descriptor, argv[first + 1]);
	} else if (is_display && first + 1 == argc) {
		result = control_display(descriptor);
	} else if (is_screen && first + 2 == argc) {
		result = control_screen(descriptor, argv[first + 1]);
	} else {
		usage();
		result = 2;
	}

	/* Closes the device and reports the command's result. */
	(void)close(descriptor);
	return result;
}

/* Reports a failed step and gives the failing exit status. */
static int
control_fail(
	const char *step,
	int error)
{
	/* The line a reader of the run sees. */
	fprintf(stderr, "amdrdnactl: %s: %s\n", step, strerror(error));
	return 1;
}

/* Prints the driver's state. */
static int
control_status(
	int descriptor)
{
	struct amdrdna_status status;
	unsigned posted;
	unsigned on_gpu;
	unsigned training;
	unsigned ours;
	unsigned irq_on;
	unsigned fault;
	unsigned hung;
	int result;

	/* Asks the driver for its state. */
	memset(&status, 0, sizeof(status));
	status.version = AMDRDNA_INTERFACE_VERSION;
	status.size = sizeof(status);
	result = ioctl(descriptor, AMDRDNA_IOC_STATUS, &status);
	if (result != 0)
		return control_fail("status", errno);

	/* Turns the flags the lines print into 0 or 1. */
	posted = (status.flags & AMDRDNA_STATUS_POSTED) != 0U;
	on_gpu = (status.flags & AMDRDNA_STATUS_GOP_ON_GPU) != 0U;
	training = (status.flags & AMDRDNA_STATUS_TRAINING_ALLOWED) != 0U;
	ours = (status.flags & AMDRDNA_STATUS_SECURE_OS_OURS) != 0U;
	irq_on = (status.flags & AMDRDNA_STATUS_INTERRUPTS_ON) != 0U;
	fault = (status.flags & AMDRDNA_STATUS_THERMAL_FAULT) != 0U;
	hung = (status.flags & AMDRDNA_STATUS_SMU_HUNG) != 0U;

	/* Prints the stage machine, the identity and what each stage read. */
	printf("stage=%s\n", stage_name(status.stage));
	printf("target=%s\n", stage_name(status.target));
	printf("running=%s\n", stage_name(status.running));
	printf("failed=%s error=%d\n", stage_name(status.failed_stage), (int)status.failed_error);
	printf("flags=0x%08x\n", (unsigned)status.flags);
	printf("pci=%08x rev=%02x subsystem=%08x\n", (unsigned)status.pci_id, (unsigned)status.pci_revision, (unsigned)status.pci_subsystem);
	printf("gc=%u.%u.%u dcn=%u.%u.%u psp=%u.%u.%u smu=%u.%u.%u\n",
	    (unsigned)(status.graphics_version >> 16), (unsigned)((status.graphics_version >> 8) & 0xffU), (unsigned)(status.graphics_version & 0xffU),
	    (unsigned)(status.display_version >> 16), (unsigned)((status.display_version >> 8) & 0xffU), (unsigned)(status.display_version & 0xffU),
	    (unsigned)(status.security_version >> 16), (unsigned)((status.security_version >> 8) & 0xffU), (unsigned)(status.security_version & 0xffU),
	    (unsigned)(status.power_version >> 16), (unsigned)((status.power_version >> 8) & 0xffU), (unsigned)(status.power_version & 0xffU));
	printf("vram=%uMiB bar0=%uMiB cpu_window=%uMiB rebar=0x%08x/0x%08x\n",
	    (unsigned)status.vram_mib, (unsigned)status.bar0_mib, (unsigned)status.cpu_window_mib,
	    (unsigned)status.rebar_capability, (unsigned)status.rebar_control);
	printf("posted=%u sos=0x%08x boot=0x%08x smu=0x%08x grbm=0x%08x scratch7=0x%08x fb=0x%llx\n",
	    posted,
	    (unsigned)status.secure_os_word, (unsigned)status.boot_mailbox, (unsigned)status.power_firmware_state,
	    (unsigned)status.engine_status, (unsigned)status.bios_scratch_7, (unsigned long long)status.frame_buffer_base);
	printf("gop otg=%u hubp=%u dig=%u kind=%u %ux%u on %ux%u shape=%u pitch=0x%x format=%u vram=+0x%llx on_gpu=%u\n",
	    (unsigned)status.gop_timing, (unsigned)status.gop_plane, (unsigned)status.gop_output, (unsigned)status.gop_output_kind,
	    (unsigned)status.gop_source_width, (unsigned)status.gop_source_height,
	    (unsigned)status.gop_width, (unsigned)status.gop_height, (unsigned)status.gop_shape,
	    (unsigned)status.gop_pitch_word, (unsigned)status.gop_format,
	    (unsigned long long)status.gop_vram_offset, on_gpu);
	printf("vbios source=%u length=%u fwinfo=%u.%u caps=0x%x pptid=%u shutdown=%uC training=%u\n",
	    (unsigned)status.vbios_source, (unsigned)status.vbios_length,
	    (unsigned)(status.firmware_table_version >> 8), (unsigned)(status.firmware_table_version & 0xffU),
	    (unsigned)status.firmware_capability_bits, (unsigned)status.power_table_id, (unsigned)status.shutdown_temperature,
	    training);
	printf("memory gart=0x%llx+%uMiB cpu_free=%uMiB gpu_free=%uMiB irq=%u signals=%u faults=%u underflow=%u\n",
	    (unsigned long long)status.gart_base, (unsigned)status.gart_mib,
	    (unsigned)status.cpu_pool_free_mib, (unsigned)status.gpu_pool_free_mib,
	    (unsigned)status.interrupt_kind, (unsigned)status.interrupt_signals,
	    (unsigned)status.vm_faults, (unsigned)status.display_underflow);
	printf("training result=%u reason=%u ms=%u\n",
	    (unsigned)status.training_result, (unsigned)status.training_reason, (unsigned)status.training_ms);
	printf("psp tmr=%uKiB programs=%u sos=0x%08x warnings=%u ours=%u irq_on=%u entries=%u unknown=%u\n",
	    (unsigned)status.region_kib, (unsigned)status.programs_loaded, (unsigned)status.secure_os_version,
	    (unsigned)status.security_warnings,
	    ours,
	    irq_on,
	    (unsigned)status.interrupt_entries, (unsigned)status.interrupt_unknown);
	printf("power smu=%u.%u.%u if=0x%x fan_mode=%u duty=%u%% fan=%urpm driven=%u%% temp=%uC max=%uC limit=%uC alerts=%u fault=%u hung=%u\n",
	    (unsigned)((status.smu_version >> 16) & 0xffU), (unsigned)((status.smu_version >> 8) & 0xffU), (unsigned)(status.smu_version & 0xffU),
	    (unsigned)status.smu_interface, (unsigned)status.fan_mode, (unsigned)status.fan_duty,
	    (unsigned)status.fan_rpm, (unsigned)status.fan_driven, (unsigned)status.temperature,
	    (unsigned)status.temperature_highest, (unsigned)status.temperature_limit, (unsigned)status.thermal_alerts,
	    fault,
	    hung);
	printf("engines flags=0x%x compute=%u gb=0x%08x rb=0x%x cu=%u tcc_off=0x%x fences=%u eop=%u traps=%u copied=%uB dispatch=%u\n",
	    (unsigned)status.engine_flags, (unsigned)status.compute_queues, (unsigned)status.address_config,
	    (unsigned)status.backends, (unsigned)status.compute_units, (unsigned)status.channels_off,
	    (unsigned)status.fences, (unsigned)status.end_of_pipe_interrupts, (unsigned)status.copy_traps,
	    (unsigned)status.copied_bytes, (unsigned)status.dispatch_words);

	/* The address spaces and the GPU device (stage A5s). */
	printf("sessions spaces=%u held=%u flushes=%u device=0x%x\n",
	    (unsigned)(status.spaces & 0xffffU), (unsigned)(status.spaces >> 16),
	    (unsigned)status.context_flushes, (unsigned)status.device_flags);
	return 0;
}

/* Asks the driver for the stages up to a named one. */
static int
control_advance(
	int descriptor,
	const char *stage)
{
	struct amdrdna_advance advance;
	uint32_t target;
	int result;

	/* The stage must be one the interface names. */
	result = stage_number(stage, &target);
	if (result != 0) {
		usage();
		return 2;
	}

	/* Asks for the stages up to it. */
	memset(&advance, 0, sizeof(advance));
	advance.version = AMDRDNA_INTERFACE_VERSION;
	advance.size = sizeof(advance);
	advance.target = target;
	result = ioctl(descriptor, AMDRDNA_IOC_ADVANCE, &advance);
	if (result != 0)
		return control_fail("advance", errno);

	/* Says what was asked; the stages run on the driver's thread. */
	printf("target=%s\n", stage_names[target]);
	return 0;
}

/* Writes the video BIOS image to a file. */
static int
control_vbios(
	int descriptor,
	const char *path)
{
	struct amdrdna_vbios_read request;
	unsigned char chunk[CONTROL_CHUNK];
	uint32_t offset;
	size_t written;
	FILE *stream;
	int result;

	/* Opens the file the image goes to. */
	stream = fopen(path, "wb");
	if (stream == NULL)
		return control_fail(path, errno);

	/* Copies the image one chunk at a time until the driver has none left. */
	offset = 0U;
	for (;;) {
		/* Asks for the chunk at the offset. */
		memset(&request, 0, sizeof(request));
		request.version = AMDRDNA_INTERFACE_VERSION;
		request.size = sizeof(request);
		request.offset = offset;
		request.length = sizeof(chunk);
		request.buffer = (uint64_t)(uintptr_t)chunk;
		result = ioctl(descriptor, AMDRDNA_IOC_VBIOS, &request);
		if (result != 0) {
			(void)fclose(stream);
			return control_fail("vbios", errno);
		}

		/* An empty chunk ends the image. */
		if (request.copied == 0U)
			break;

		/* Writes the chunk. */
		written = fwrite(chunk, 1U, request.copied, stream);
		if (written != request.copied) {
			(void)fclose(stream);
			return control_fail(path, EIO);
		}

		/* Moves on, and stops at the image's end. */
		offset += request.copied;
		if (offset >= request.total)
			break;
	}

	/* Closes the file; a failed close loses the image. */
	result = fclose(stream);
	if (result != 0)
		return control_fail(path, errno);

	/* Says where the image went. */
	printf("vbios=%s bytes=%u\n", path, (unsigned)offset);
	return 0;
}

/* Prints the display the driver took over: its picture, its addresses, its registers and its counts. */
static int
control_display(
	int descriptor)
{
	struct amdrdna_display_state state;
	int result;

	/* Asks the driver for the display's state. */
	memset(&state, 0, sizeof(state));
	state.version = AMDRDNA_INTERFACE_VERSION;
	state.size = sizeof(state);
	result = ioctl(descriptor, AMDRDNA_IOC_DISPLAY, &state);
	if (result != 0)
		return control_fail("display", errno);

	/* The takeover, the picture and the addresses. */
	printf("display flags=0x%04x otg=%u hubp=%u hpd=%u %ux%u stride=%u refresh=%u.%03uHz\n",
	    (unsigned)state.flags, (unsigned)state.timing, (unsigned)state.plane, (unsigned)state.hotplug,
	    (unsigned)state.width, (unsigned)state.height, (unsigned)state.stride,
	    (unsigned)(state.refresh_millihz / 1000U), (unsigned)(state.refresh_millihz % 1000U));
	printf("address console=0x%llx requested=0x%llx shown=0x%llx plane=0x%llx cursor=0x%llx\n",
	    (unsigned long long)state.console_address, (unsigned long long)state.requested_address,
	    (unsigned long long)state.shown_address, (unsigned long long)state.plane_address,
	    (unsigned long long)state.cursor_address);

	/* The registers as read now. */
	printf("timing frame=0x%06x position=0x%08x sync=0x%08x combiner=0x%08x\n",
	    (unsigned)state.frame_count, (unsigned)state.position, (unsigned)state.sync_status, (unsigned)state.combiner_input);
	printf("plane control=0x%08x flip=0x%08x flip_irq=0x%08x\n",
	    (unsigned)state.plane_control, (unsigned)state.flip_control, (unsigned)state.flip_interrupt);
	printf("cursor control=0x%08x size=0x%08x position=0x%08x hot=0x%08x pixel=0x%08x\n",
	    (unsigned)state.cursor_control, (unsigned)state.cursor_size, (unsigned)state.cursor_position,
	    (unsigned)state.cursor_hot_spot, (unsigned)state.pixel_cursor);
	printf("hotplug status=0x%08x control=0x%08x\n", (unsigned)state.hotplug_status, (unsigned)state.hotplug_control);

	/* The counts. */
	printf("counts frame_irq=%llu flip_irq=%llu hotplug_irq=%llu flips=%llu refreshes=%llu topology=%llu presents=%llu leases=%llu\n",
	    (unsigned long long)state.frame_signals, (unsigned long long)state.flip_signals,
	    (unsigned long long)state.hotplug_signals, (unsigned long long)state.flips,
	    (unsigned long long)state.refresh_count, (unsigned long long)state.topology,
	    (unsigned long long)state.presents, (unsigned long long)state.leases);
	return 0;
}

/*
 * Writes the picture the display shows to a file as a binary PPM (P6):
 * the driver's rows of 32-bit B, G, R, A pixels become R, G, B triples.
 */
static int
control_screen(
	int descriptor,
	const char *path)
{
	struct amdrdna_screen screen;
	unsigned char *pixels;
	unsigned char *rgb;
	FILE *stream;
	size_t bytes;
	size_t written;
	uint32_t x;
	uint32_t y;
	int result;

	/* A buffer for the largest picture the driver keeps: 4096 by 2160. */
	bytes = (size_t)16384U * 2160U;
	pixels = malloc(bytes);
	if (pixels == NULL)
		return control_fail("screen", ENOMEM);

	/* Asks the driver for the rows. */
	memset(&screen, 0, sizeof(screen));
	screen.version = AMDRDNA_INTERFACE_VERSION;
	screen.size = sizeof(screen);
	screen.buffer = (uint64_t)(uintptr_t)pixels;
	screen.length = (uint32_t)bytes;
	result = ioctl(descriptor, AMDRDNA_IOC_SCREEN, &screen);
	if (result != 0) {
		free(pixels);
		return control_fail("screen", errno);
	}

	/* The R, G, B triples of every row copied. */
	rgb = malloc((size_t)screen.width * screen.height * 3U);
	if (rgb == NULL) {
		free(pixels);
		return control_fail("screen", ENOMEM);
	}

	/* Each row the driver copied, pixel by pixel. */
	for (y = 0U; y < screen.height && (size_t)(y + 1U) * screen.stride <= screen.copied; y++) {
		for (x = 0U; x < screen.width; x++) {
			/* B, G, R, A in, R, G, B out. */
			rgb[((size_t)y * screen.width + x) * 3U] = pixels[(size_t)y * screen.stride + x * 4U + 2U];
			rgb[((size_t)y * screen.width + x) * 3U + 1U] = pixels[(size_t)y * screen.stride + x * 4U + 1U];
			rgb[((size_t)y * screen.width + x) * 3U + 2U] = pixels[(size_t)y * screen.stride + x * 4U];
		}
	}

	/* Writes the file. */
	stream = fopen(path, "wb");
	if (stream == NULL) {
		free(rgb);
		free(pixels);
		return control_fail(path, errno);
	}

	/* The header, the rows, the close. */
	fprintf(stream, "P6\n%u %u\n255\n", (unsigned)screen.width, (unsigned)y);
	written = fwrite(rgb, 1U, (size_t)screen.width * y * 3U, stream);
	result = fclose(stream);
	free(rgb);
	free(pixels);
	if (written != (size_t)screen.width * y * 3U || result != 0)
		return control_fail(path, EIO);

	/* Says what was written. */
	printf("screen=%s %ux%u stride=%u address=0x%llx\n", path, (unsigned)screen.width, (unsigned)y,
	    (unsigned)screen.stride, (unsigned long long)screen.address);
	return 0;
}

/* Names a stage number. */
static const char *
stage_name(
	uint32_t stage)
{
	/* A number past the last stage has no name. */
	if (stage >= AMDRDNA_STAGE_COUNT)
		return "?";

	/* The stage's name. */
	return stage_names[stage];
}

/* Finds a stage by its name; 0, or 1 for a name the interface does not have. */
static int
stage_number(
	const char *name,
	uint32_t *stage)
{
	uint32_t number;
	int compared;

	/* Compares the name with every stage's. */
	for (number = 1U; number < AMDRDNA_STAGE_COUNT; number++) {
		/* Stops at the stage of that name. */
		compared = strcmp(stage_names[number], name);
		if (compared == 0) {
			*stage = number;
			return 0;
		}
	}

	/* No stage has the name. */
	return 1;
}

/* Prints the command line. */
static void
usage(void)
{
	/* The one usage line. */
	fprintf(stderr, "usage: amdrdnactl [-f DEVICE] [status | advance STAGE | vbios FILE | display | screen FILE] | amdrdnactl gpu|modes [GPU-DEVICE]\n");
}
