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

/* The default device: the first GPU's. */
#define CONTROL_DEVICE		"/dev/amdrdna0"

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
	int result;

	/* Asks the driver for its state. */
	memset(&status, 0, sizeof(status));
	status.version = AMDRDNA_INTERFACE_VERSION;
	status.size = sizeof(status);
	result = ioctl(descriptor, AMDRDNA_IOC_STATUS, &status);
	if (result != 0)
		return control_fail("status", errno);

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
	    (status.flags & AMDRDNA_STATUS_POSTED) != 0U ? 1U : 0U,
	    (unsigned)status.secure_os_word, (unsigned)status.boot_mailbox, (unsigned)status.power_firmware_state,
	    (unsigned)status.engine_status, (unsigned)status.bios_scratch_7, (unsigned long long)status.frame_buffer_base);
	printf("gop otg=%u hubp=%u dig=%u kind=%u %ux%u pitch=0x%x format=%u vram=+0x%llx on_gpu=%u\n",
	    (unsigned)status.gop_timing, (unsigned)status.gop_plane, (unsigned)status.gop_output, (unsigned)status.gop_output_kind,
	    (unsigned)status.gop_width, (unsigned)status.gop_height, (unsigned)status.gop_pitch_word, (unsigned)status.gop_format,
	    (unsigned long long)status.gop_vram_offset, (status.flags & AMDRDNA_STATUS_GOP_ON_GPU) != 0U ? 1U : 0U);
	printf("vbios source=%u length=%u fwinfo=%u.%u caps=0x%x pptid=%u shutdown=%uC training=%u\n",
	    (unsigned)status.vbios_source, (unsigned)status.vbios_length,
	    (unsigned)(status.firmware_table_version >> 8), (unsigned)(status.firmware_table_version & 0xffU),
	    (unsigned)status.firmware_capability_bits, (unsigned)status.power_table_id, (unsigned)status.shutdown_temperature,
	    (status.flags & AMDRDNA_STATUS_TRAINING_ALLOWED) != 0U ? 1U : 0U);
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
	fprintf(stderr, "usage: amdrdnactl [-f DEVICE] [status | advance STAGE | vbios FILE]\n");
}
