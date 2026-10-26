/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI GPU status tool: lists every GPU node whose driver is the
 * generic GHCI driver, with its name (ghci:<vendor>:<device>), the
 * capabilities the device offers, its allocation limits and, when it has
 * a display, the display's modes.  As root it also runs the driver's
 * diagnostics of the paths to host and local memory (each step goes to the
 * kernel log), and loads a new image of the command processor's firmware
 * (from host memory, or with -l through the GPU's local memory).
 *
 *   ghcictl [list]
 *   ghcictl diag [ROUNDS]
 *   ghcictl fwload [-l] IMEM.bin DMEM.bin
 */

#include <uapi/gpu.h>
#include <uapi/gpu-display.h>
#include <uapi/gpu-op.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* The GPU nodes looked at, and the prefix of the GHCI driver's names. */
#define NODES_MAX			16
#define GHCI_PREFIX			"ghci:"

/* The most modes listed for a display. */
#define MODES_MAX			16U

/* The diagnostics' default and largest rounds. */
#define DIAG_ROUNDS			16U
#define DIAG_ROUNDS_MAX			100000U

/* The words of the firmware's two memories, and the words sent in one command. */
#define IMEM_WORDS			32768U
#define DMEM_WORDS			16384U
#define CHUNK_WORDS			8192U

/* GPU_OP_GHCI_FIRMWARE: the head's words, the parts and the flags (uapi/gpu-op.h). */
#define FIRMWARE_HEAD_WORDS		5U
#define FIRMWARE_PART_IMEM		0U
#define FIRMWARE_PART_DMEM		1U
#define FIRMWARE_FLAG_LOAD		0x1U
#define FIRMWARE_FLAG_VIA_LOCAL		0x2U

/*
 * The names of the capability bits a device reports.
 *
 * Values referenced: zedBSD include/uapi/gpu.h, gpu-display.h,
 * gpu-allocation.h, gpu-fence.h and gpu-job.h (GPU_CAP_*).
 */
static const struct {
	uint32_t bit;
	const char *name;
} capability_names[] = {
	{ 1U, "resource" },
	{ 2U, "capset" },
	{ 4U, "blob" },
	{ 8U, "transfer" },
	{ 16U, "command" },
	{ 32U, "present" },
	{ 64U, "mapping" },
	{ 128U, "display" },
	{ 256U, "share" },
	{ 512U, "notification" },
	{ 1024U, "allocation-share" },
	{ 2048U, "fence" },
	{ 4096U, "display-events" },
	{ 8192U, "job" },
	{ 16384U, "job-capacity" },
	{ 32768U, "display-control" }
};

/*
 * The images of a reload, read from their files, and one command of it
 * (its head and a chunk of words).
 */
static uint32_t imem_image[IMEM_WORDS];
static uint32_t dmem_image[DMEM_WORDS];
static uint32_t command_words[FIRMWARE_HEAD_WORDS + CHUNK_WORDS];

static int list_all(void);
static int list_node(const char *path);
static void list_display(int fd);
static int open_ghci(char *path, size_t size);
static int send_command(int fd, const uint32_t *words, uint32_t count);
static int run_diag(int argc, char **argv);
static int run_fwload(int argc, char **argv);
static int read_image(const char *path, uint32_t *words, uint32_t limit, uint32_t *count);
static int send_part(int fd, uint32_t part, const uint32_t *words, uint32_t count, uint32_t last_flags);
static void usage(void);

int
main(
	int argc,
	char **argv)
{
	int status;

	/* The list, the default. */
	if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0)) {
		status = list_all();
		return status;
	}

	/* The diagnostics. */
	if (strcmp(argv[1], "diag") == 0) {
		status = run_diag(argc - 2, argv + 2);
		return status;
	}

	/* The firmware's reload. */
	if (strcmp(argv[1], "fwload") == 0) {
		status = run_fwload(argc - 2, argv + 2);
		return status;
	}

	/* Anything else. */
	usage();
	return 2;
}

/* Lists every GHCI node: 0, or 1 when there is none. */
static int
list_all(void)
{
	char path[32];
	int index;
	int found;

	/* Every GPU node, those of the GHCI driver listed. */
	found = 0;
	for (index = 0; index < NODES_MAX; index++) {
		snprintf(path, sizeof(path), "/dev/gpu%d", index);
		found += list_node(path);
	}

	/* Says when there is none. */
	if (found == 0) {
		printf("ghcictl: no GHCI GPU\n");
		return 1;
	}

	/* Succeeded: the nodes are listed. */
	return 0;
}

/* Lists one node when it is a GHCI device; 1 when it was. */
static int
list_node(
	const char *path)
{
	struct gpu_info info;
	unsigned index;
	int status;
	int fd;

	/* A node that does not open, or of another driver, is skipped. */
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return 0;
	memset(&info, 0, sizeof(info));
	info.version = GPU_ABI_VERSION;
	info.size = sizeof(info);
	status = ioctl(fd, GPU_GET_INFO, &info);
	if (status != 0 || strncmp(info.driver_name, GHCI_PREFIX, strlen(GHCI_PREFIX)) != 0) {
		close(fd);
		return 0;
	}

	/* The name, the limits and the capabilities. */
	printf("%s: %s\n", path, info.driver_name);
	printf("  resources: at most %u per open, %llu bytes each\n", info.max_resources, (unsigned long long)info.max_resource_bytes);
	printf("  capabilities:");
	for (index = 0U; index < sizeof(capability_names) / sizeof(capability_names[0]); index++) {
		if ((info.capabilities & capability_names[index].bit) != 0U)
			printf(" %s", capability_names[index].name);
	}
	printf("\n");

	/* The display and its modes. */
	if ((info.capabilities & GPU_CAP_DISPLAY) != 0U)
		list_display(fd);
	close(fd);
	return 1;
}

/* Lists the display of a node and its modes. */
static void
list_display(
	int fd)
{
	struct gpu_display_info display;
	struct gpu_display_mode mode;
	unsigned index;
	int status;

	/* The first output. */
	memset(&display, 0, sizeof(display));
	display.version = GPU_ABI_VERSION;
	display.size = sizeof(display);
	status = ioctl(fd, GPU_DISPLAY_QUERY, &display);
	if (status != 0) {
		printf("  display: not answered (%d)\n", errno);
		return;
	}
	printf("  display %u \"%s\": %ux%u now, %s\n", display.display_id, display.name, display.current_width, display.current_height,
	    (display.flags & GPU_DISPLAY_ACTIVE) != 0U ? "lit" : "dark");

	/* Its modes. */
	for (index = 0U; index < MODES_MAX; index++) {
		memset(&mode, 0, sizeof(mode));
		mode.version = GPU_ABI_VERSION;
		mode.size = sizeof(mode);
		mode.display_id = display.display_id;
		mode.generation = display.generation;
		mode.operation = GPU_DISPLAY_MODE_ENUMERATE;
		mode.index = index;
		status = ioctl(fd, GPU_DISPLAY_MODE, &mode);
		if (status != 0)
			break;
		printf("  mode %u: %ux%u at %u.%03u Hz\n", index, mode.width, mode.height, mode.refresh_millihz / 1000U, mode.refresh_millihz % 1000U);
	}
}

/* Opens the first GHCI node and names it in path: the descriptor, or -1. */
static int
open_ghci(
	char *path,
	size_t size)
{
	struct gpu_info info;
	int index;
	int status;
	int other;
	int fd;

	/* Every GPU node until one is the GHCI driver's. */
	for (index = 0; index < NODES_MAX; index++) {
		snprintf(path, size, "/dev/gpu%d", index);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			/* A node that is there but does not open says why. */
			if (errno != ENOENT)
				fprintf(stderr, "ghcictl: %s: %s\n", path, strerror(errno));
			continue;
		}
		memset(&info, 0, sizeof(info));
		info.version = GPU_ABI_VERSION;
		info.size = sizeof(info);
		status = ioctl(fd, GPU_GET_INFO, &info);
		other = 1;
		if (status == 0)
			other = strncmp(info.driver_name, GHCI_PREFIX, strlen(GHCI_PREFIX));
		if (other == 0)
			return fd;
		close(fd);
	}

	/* None. */
	fprintf(stderr, "ghcictl: no GHCI GPU\n");
	return -1;
}

/* Sends one command of words to the driver: 0, or the errno. */
static int
send_command(
	int fd,
	const uint32_t *words,
	uint32_t count)
{
	struct gpu_command command;
	int status;

	/* GPU_COMMAND with the words. */
	memset(&command, 0, sizeof(command));
	command.version = GPU_ABI_VERSION;
	command.size = sizeof(command);
	command.address = (uint64_t)(uintptr_t)words;
	command.bytes = count * 4U;
	status = ioctl(fd, GPU_COMMAND, &command);
	if (status != 0)
		return errno;

	/* Succeeded: the driver took it. */
	return 0;
}

/* ghcictl diag [ROUNDS]: runs the driver's diagnostics; the steps are in the kernel log. */
static int
run_diag(
	int argc,
	char **argv)
{
	uint32_t words[3];
	unsigned long rounds;
	char path[32];
	char *end;
	int error;
	int fd;

	/* The rounds, 16 unless given. */
	rounds = DIAG_ROUNDS;
	if (argc > 1) {
		usage();
		return 2;
	}
	if (argc == 1) {
		rounds = strtoul(argv[0], &end, 10);
		if (*end != '\0' || rounds == 0UL || rounds > DIAG_ROUNDS_MAX) {
			usage();
			return 2;
		}
	}

	/* The node. */
	fd = open_ghci(path, sizeof(path));
	if (fd < 0)
		return 1;

	/* The command: GPU_OP_GHCI_DIAG, no flags, the rounds. */
	words[0] = GPU_OP_GHCI_DIAG;
	words[1] = 0U;
	words[2] = (uint32_t)rounds;
	printf("ghcictl: %s: diag, %lu rounds (each step in the kernel log)\n", path, rounds);
	error = send_command(fd, words, 3U);
	close(fd);

	/* The outcome: every step passed, a step failed, or the run stopped. */
	if (error == 0) {
		printf("ghcictl: diag: PASS\n");
		return 0;
	}
	if (error == EIO) {
		printf("ghcictl: diag: FAIL (a step failed; see the kernel log)\n");
		return 1;
	}
	printf("ghcictl: diag: not run to its end: %s (see the kernel log)\n", strerror(error));
	return 1;
}

/* ghcictl fwload [-l] IMEM.bin DMEM.bin: loads a new firmware image and starts it. */
static int
run_fwload(
	int argc,
	char **argv)
{
	uint32_t imem_count;
	uint32_t dmem_count;
	uint32_t flags;
	char path[32];
	int error;
	int fd;

	/* -l: through local memory. */
	flags = FIRMWARE_FLAG_LOAD;
	if (argc == 3 && strcmp(argv[0], "-l") == 0) {
		flags |= FIRMWARE_FLAG_VIA_LOCAL;
		argc--;
		argv++;
	}
	if (argc != 2) {
		usage();
		return 2;
	}

	/* The two images. */
	error = read_image(argv[0], imem_image, IMEM_WORDS, &imem_count);
	if (error != 0)
		return 1;
	error = read_image(argv[1], dmem_image, DMEM_WORDS, &dmem_count);
	if (error != 0)
		return 1;

	/* The node. */
	fd = open_ghci(path, sizeof(path));
	if (fd < 0)
		return 1;

	/* IMEM's words, then DMEM's, the last command asking for the load. */
	printf("ghcictl: %s: loading %u IMEM words and %u DMEM words%s\n",
	    path,
	    imem_count,
	    dmem_count,
	    (flags & FIRMWARE_FLAG_VIA_LOCAL) != 0U ? " through local memory" : "");
	error = send_part(fd, FIRMWARE_PART_IMEM, imem_image, imem_count, 0U);
	if (error == 0)
		error = send_part(fd, FIRMWARE_PART_DMEM, dmem_image, dmem_count, flags);
	close(fd);

	/* The outcome: the new firmware answered its INIT, or the kernel log says why not. */
	if (error != 0) {
		printf("ghcictl: fwload: failed: %s (see the kernel log)\n", strerror(error));
		return 1;
	}

	/* Succeeded: the new firmware runs. */
	printf("ghcictl: fwload: the new firmware runs\n");
	return 0;
}

/* Reads an image file of whole words, at most limit: 0, or -1 with the reason printed. */
static int
read_image(
	const char *path,
	uint32_t *words,
	uint32_t limit,
	uint32_t *count)
{
	unsigned char *bytes;
	unsigned char extra;
	size_t total;
	ssize_t got;
	ssize_t more;
	int fd;

	/* The file. */
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "ghcictl: %s: %s\n", path, strerror(errno));
		return -1;
	}

	/* Its bytes, as many as the memory holds. */
	bytes = (unsigned char *)words;
	total = 0U;
	got = 0;
	for (;;) {
		got = read(fd, bytes + total, limit * 4U - total);
		if (got <= 0)
			break;
		total += (size_t)got;
		if (total == limit * 4U)
			break;
	}

	/* One byte more would be a file longer than the memory. */
	more = 0;
	if (got > 0)
		more = read(fd, &extra, 1U);
	close(fd);
	if (more > 0) {
		fprintf(stderr, "ghcictl: %s: longer than the %u words of the memory\n", path, limit);
		return -1;
	}

	/* Read errors, an empty file, a file longer than the memory, a part of a word. */
	if (got < 0) {
		fprintf(stderr, "ghcictl: %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (total == 0U || (total % 4U) != 0U) {
		fprintf(stderr, "ghcictl: %s: %lu bytes, not whole words\n", path, (unsigned long)total);
		return -1;
	}

	/* Succeeded: the words (the image is little endian, as the CPU of the GPU). */
	*count = (uint32_t)(total / 4U);
	return 0;
}

/* Sends a part of the image in chunks; the last chunk carries last_flags. */
static int
send_part(
	int fd,
	uint32_t part,
	const uint32_t *words,
	uint32_t count,
	uint32_t last_flags)
{
	uint32_t offset;
	uint32_t length;
	int error;

	/* One chunk after another. */
	for (offset = 0U; offset < count; offset += length) {
		length = count - offset;
		if (length > CHUNK_WORDS)
			length = CHUNK_WORDS;
		command_words[0] = GPU_OP_GHCI_FIRMWARE;
		command_words[1] = 0U;
		if (offset + length == count)
			command_words[1] = last_flags;
		command_words[2] = part;
		command_words[3] = offset;
		command_words[4] = length;
		memcpy(&command_words[FIRMWARE_HEAD_WORDS], &words[offset], length * 4U);
		error = send_command(fd, command_words, FIRMWARE_HEAD_WORDS + length);
		if (error != 0)
			return error;
	}

	/* Succeeded: every word was taken. */
	return 0;
}

/* Prints how the tool is used. */
static void
usage(void)
{
	fprintf(stderr, "usage: ghcictl [list]\n");
	fprintf(stderr, "       ghcictl diag [ROUNDS]\n");
	fprintf(stderr, "       ghcictl fwload [-l] IMEM.bin DMEM.bin\n");
}
