/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * pvr-ioctl-shim (ws270-p007, q1547): an LD_PRELOAD library for Linux on
 * the VisionFive 2 with the IMG DDK.  It wraps ioctl() and, for the DRM
 * ioctl that carries the services bridge (DRM_IOCTL_PVR_SRVKM_CMD), writes
 * what user space hands the kernel in two calls of the RGXTA3D bridge
 * (group 130):
 *
 *   func 12 (RGXKickTA3D2)       the in structure's counts, sizes and
 *                                flags, then the TA, 3D and 3D PR
 *                                commands as 32-bit words
 *   func 0 (RGXCreateHWRTDataSet) the in structure's values and the
 *                                device addresses its arrays name
 *
 * The values and the structures' shapes are written here from the
 * kernel's bridge headers (MIT/GPL); no header is included.  Every
 * record is appended to the file named by PVR_SHIM_OUT (nothing is
 * written without it), the first PVR_SHIM_MAX (8) calls of each kind a
 * process after skipping PVR_SHIM_SKIP (0).  User memory is read with
 * process_vm_readv, so a bad pointer or a short array shows as unread
 * rather than ending the process.  The commands are the proprietary
 * driver's output: keep the file out of git.
 *
 * Built on the board: gcc -shared -fPIC -O2 -o pvr-ioctl-shim.so pvr-ioctl-shim.c -ldl
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

/* _IOWR('d', DRM_COMMAND_BASE + DRM_PVR_SRVKM_CMD, struct of 32 bytes): 3 << 30 | 32 << 16 | 'd' << 8 | 0x40. */
#define SHIM_IOCTL_SRVKM_CMD	0xc0206440UL

/* The RGXTA3D bridge group and its two functions. */
#define SHIM_BRIDGE_RGXTA3D	130U
#define SHIM_FUNC_HWRTDATASET	0U
#define SHIM_FUNC_KICKTA3D2	12U

/* The calls of each kind written a process by default. */
#define SHIM_DEFAULT_MAX	8UL

/* The most bytes of one command written (larger ones are cut). */
#define SHIM_COMMAND_MAX	65536U

/* The device addresses read from each of HWRTDataSet's arrays (at most RGXMKIF_NUM_RTDATAS). */
#define SHIM_ARRAY_ENTRIES	4U

/* The ioctl's argument: struct drm_pvr_srvkm_cmd. */
struct shim_srvkm_cmd {
	uint32_t bridge_id;
	uint32_t bridge_func_id;
	uint64_t in_data_ptr;
	uint64_t out_data_ptr;
	uint32_t in_data_size;
	uint32_t out_data_size;
};

/* PVRSRV_BRIDGE_IN_RGXKICKTA3D2 on LP64 (packed; pointers and handles 8 bytes, fences 4, bools 1). */
struct __attribute__((packed)) shim_kick_in {
	uint64_t deadline;
	uint64_t km_hwrt_dataset;
	uint64_t msaa_scratch_buffer;
	uint64_t pr_fence_ufo_sync_prim_block;
	uint64_t render_context;
	uint64_t zs_buffer;
	uint64_t client_3d_update_sync_offset;
	uint64_t client_3d_update_value;
	uint64_t client_ta_fence_sync_offset;
	uint64_t client_ta_fence_value;
	uint64_t client_ta_update_sync_offset;
	uint64_t client_ta_update_value;
	uint64_t sync_pmr_flags;
	uint64_t cmd_3d;
	uint64_t cmd_3d_pr;
	uint64_t cmd_ta;
	uint64_t update_fence_name;
	uint64_t update_fence_name_3d;
	uint64_t client_3d_update_sync_prim_block;
	uint64_t client_ta_fence_sync_prim_block;
	uint64_t client_ta_update_sync_prim_block;
	uint64_t sync_pmrs;
	int32_t check_fence;
	int32_t check_fence_3d;
	int32_t update_timeline;
	int32_t update_timeline_3d;
	uint32_t cmd_3d_size;
	uint32_t cmd_3d_pr_size;
	uint32_t client_3d_update_count;
	uint32_t client_ta_fence_count;
	uint32_t client_ta_update_count;
	uint32_t ext_job_ref;
	uint32_t number_of_draw_calls;
	uint32_t number_of_indices;
	uint32_t number_of_mrts;
	uint32_t pdump_flags;
	uint32_t pr_fence_ufo_sync_offset;
	uint32_t pr_fence_value;
	uint32_t render_target_size;
	uint32_t sync_pmr_count;
	uint32_t cmd_ta_size;
	uint8_t abort;
	uint8_t kick_3d;
	uint8_t kick_pr;
	uint8_t kick_ta;
};

/* PVRSRV_BRIDGE_IN_RGXCREATEHWRTDATASET on LP64 (packed). */
struct __attribute__((packed)) shim_hwrt_in {
	uint64_t flipped_multi_sample_ctl;
	uint64_t multi_sample_ctl;
	uint64_t macrotile_array;
	uint64_t pm_mlist;
	uint64_t rtc;
	uint64_t rgn_header;
	uint64_t tail_ptrs;
	uint64_t vheap_table;
	uint64_t km_hwrt_dataset;
	uint64_t free_lists;
	uint32_t isp_merge_lower_x;
	uint32_t isp_merge_lower_y;
	uint32_t isp_merge_scale_x;
	uint32_t isp_merge_scale_y;
	uint32_t isp_merge_upper_x;
	uint32_t isp_merge_upper_y;
	uint32_t isp_mtile_size;
	uint32_t mtile_stride;
	uint32_t ppp_screen;
	uint32_t rgn_header_size;
	uint32_t te_aa;
	uint32_t te_mtile1;
	uint32_t te_mtile2;
	uint32_t te_screen;
	uint32_t tpc_size;
	uint32_t tpc_stride;
	uint16_t max_rts;
};

_Static_assert(sizeof(struct shim_srvkm_cmd) == 32, "drm_pvr_srvkm_cmd is 32 bytes");
_Static_assert(sizeof(struct shim_kick_in) == 256, "RGXKICKTA3D2's in structure is 256 bytes on LP64");
_Static_assert(sizeof(struct shim_hwrt_in) == 146, "RGXCREATEHWRTDATASET's in structure is 146 bytes on LP64");

/* The C library's ioctl, found once; guarded by shim_once. */
static int (*shim_real_ioctl)(int, unsigned long, ...);

/* Runs shim_setup once a process. */
static pthread_once_t shim_once = PTHREAD_ONCE_INIT;

/* The output file's name (PVR_SHIM_OUT), the calls to skip and the most to write; set by shim_setup. */
static const char *shim_out;
static unsigned long shim_skip;
static unsigned long shim_max;

/* The calls of each kind seen, and the file's writes; guarded by shim_lock. */
static pthread_mutex_t shim_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long shim_kicks_seen;
static unsigned long shim_hwrts_seen;

static void shim_setup(void);
static unsigned long shim_number(const char *name, unsigned long fallback);
static size_t shim_read(uint64_t address, void *buffer, size_t size);
static void shim_words(FILE *file, const char *what, uint64_t address, uint32_t size);
static void shim_kick(FILE *file, unsigned long number, const struct shim_srvkm_cmd *cmd);
static void shim_hwrt(FILE *file, unsigned long number, const struct shim_srvkm_cmd *cmd);
static void shim_record(const struct shim_srvkm_cmd *cmd);

/* Wraps the C library's ioctl, writing the bridge calls asked for before passing every call on. */
int
ioctl(
	int fd,
	unsigned long request,
	...)
{
	va_list arguments;
	void *argument;
	int result;

	/* The one argument every ioctl here takes. */
	va_start(arguments, request);
	argument = va_arg(arguments, void *);
	va_end(arguments);

	/* The C library's ioctl and the settings. */
	pthread_once(&shim_once, shim_setup);
	if (shim_real_ioctl == NULL)
		return -1;

	/* The services bridge's calls, recorded before the kernel sees them. */
	if (shim_out != NULL && (request & 0xffffffffUL) == SHIM_IOCTL_SRVKM_CMD && argument != NULL)
		shim_record(argument);

	/* Succeeded: the call passed on. */
	result = shim_real_ioctl(fd, request, argument);
	return result;
}

/* Finds the C library's ioctl and reads the settings. */
static void
shim_setup(
	void)
{
	/* The next ioctl after this library's. */
	shim_real_ioctl = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");

	/* The file, the calls skipped and the most written. */
	shim_out = getenv("PVR_SHIM_OUT");
	shim_skip = shim_number("PVR_SHIM_SKIP", 0);
	shim_max = shim_number("PVR_SHIM_MAX", SHIM_DEFAULT_MAX);
}

/* Reads a number from the environment, or the fallback when it is unset or not a number. */
static unsigned long
shim_number(
	const char *name,
	unsigned long fallback)
{
	const char *text;
	char *end;
	unsigned long value;

	/* Unset: the fallback. */
	text = getenv(name);
	if (text == NULL || *text == '\0')
		return fallback;

	/* Not a number: the fallback. */
	value = strtoul(text, &end, 0);
	if (*end != '\0')
		return fallback;

	/* Succeeded: the number. */
	return value;
}

/* Copies this process's memory without faulting; returns the bytes read (0 for a bad address). */
static size_t
shim_read(
	uint64_t address,
	void *buffer,
	size_t size)
{
	struct iovec local;
	struct iovec remote;
	ssize_t got;

	/* Nothing at a null address. */
	if (address == 0 || size == 0)
		return 0;

	/* The copy through the kernel, which reports a bad address as an error. */
	local.iov_base = buffer;
	local.iov_len = size;
	remote.iov_base = (void *)(uintptr_t)address;
	remote.iov_len = size;
	got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
	if (got < 0)
		return 0;

	/* Succeeded: what was read. */
	return (size_t)got;
}

/* Writes one command as little-endian 32-bit words, eight a line with the byte offset. */
static void
shim_words(
	FILE *file,
	const char *what,
	uint64_t address,
	uint32_t size)
{
	uint8_t *bytes;
	size_t wanted;
	size_t got;
	size_t at;
	uint32_t word;

	/* The header line; nothing more for an empty command. */
	fprintf(file, "%s: address 0x%016llx size %u\n", what, (unsigned long long)address, (unsigned)size);
	if (address == 0 || size == 0)
		return;

	/* The bytes, cut at SHIM_COMMAND_MAX. */
	wanted = size;
	if (wanted > SHIM_COMMAND_MAX)
		wanted = SHIM_COMMAND_MAX;
	bytes = calloc(1, wanted + 4U);
	if (bytes == NULL) {
		fprintf(file, "%s: no memory\n", what);
		return;
	}
	got = shim_read(address, bytes, wanted);
	if (got != wanted)
		fprintf(file, "%s: read %lu of %lu bytes\n", what, (unsigned long)got, (unsigned long)wanted);

	/* Each word, eight a line (a last partial word padded with zeroes). */
	for (at = 0; at < got; at += 4U) {
		if (at % 32U == 0)
			fprintf(file, "%s %06lx:", what, (unsigned long)at);
		word = (uint32_t)bytes[at] | (uint32_t)bytes[at + 1U] << 8 | (uint32_t)bytes[at + 2U] << 16 | (uint32_t)bytes[at + 3U] << 24;
		fprintf(file, " %08x", (unsigned)word);
		if (at % 32U == 28U || at + 4U >= got)
			fprintf(file, "\n");
	}
	free(bytes);
}

/* Writes one RGXKickTA3D2 call: its values, then the three commands. */
static void
shim_kick(
	FILE *file,
	unsigned long number,
	const struct shim_srvkm_cmd *cmd)
{
	struct shim_kick_in in;
	size_t got;

	/* The header; a structure of another size is a layout this shim does not know. */
	fprintf(file, "== kick %lu pid %ld in_size %u out_size %u\n", number, (long)getpid(), (unsigned)cmd->in_data_size, (unsigned)cmd->out_data_size);
	if (cmd->in_data_size != sizeof(in)) {
		fprintf(file, "kick: in_size is not %lu, raw words follow\n", (unsigned long)sizeof(in));
		shim_words(file, "in", cmd->in_data_ptr, cmd->in_data_size);
		return;
	}

	/* The in structure. */
	memset(&in, 0, sizeof(in));
	got = shim_read(cmd->in_data_ptr, &in, sizeof(in));
	if (got != sizeof(in)) {
		fprintf(file, "kick: in structure unreadable\n");
		return;
	}

	/* Its values. */
	fprintf(file, "kick_ta %u kick_3d %u kick_pr %u abort %u\n", (unsigned)in.kick_ta, (unsigned)in.kick_3d, (unsigned)in.kick_pr, (unsigned)in.abort);
	fprintf(file, "ta_size %u 3d_size %u 3d_pr_size %u\n", (unsigned)in.cmd_ta_size, (unsigned)in.cmd_3d_size, (unsigned)in.cmd_3d_pr_size);
	fprintf(file, "draw_calls %u indices %u mrts %u render_target_size %u ext_job_ref %u pdump_flags 0x%x\n", (unsigned)in.number_of_draw_calls,
		(unsigned)in.number_of_indices, (unsigned)in.number_of_mrts, (unsigned)in.render_target_size, (unsigned)in.ext_job_ref,
		(unsigned)in.pdump_flags);
	fprintf(file, "ta_fence_count %u ta_update_count %u 3d_update_count %u sync_pmr_count %u pr_fence_offset %u pr_fence_value %u\n",
		(unsigned)in.client_ta_fence_count, (unsigned)in.client_ta_update_count, (unsigned)in.client_3d_update_count,
		(unsigned)in.sync_pmr_count, (unsigned)in.pr_fence_ufo_sync_offset, (unsigned)in.pr_fence_value);
	fprintf(file, "check_fence %d check_fence_3d %d update_timeline %d update_timeline_3d %d deadline %llu\n", (int)in.check_fence,
		(int)in.check_fence_3d, (int)in.update_timeline, (int)in.update_timeline_3d, (unsigned long long)in.deadline);
	fprintf(file, "handles hwrt 0x%llx render_context 0x%llx zs 0x%llx msaa 0x%llx\n", (unsigned long long)in.km_hwrt_dataset,
		(unsigned long long)in.render_context, (unsigned long long)in.zs_buffer, (unsigned long long)in.msaa_scratch_buffer);

	/* The three commands. */
	shim_words(file, "ta", in.cmd_ta, in.cmd_ta_size);
	shim_words(file, "3d", in.cmd_3d, in.cmd_3d_size);
	shim_words(file, "3dpr", in.cmd_3d_pr, in.cmd_3d_pr_size);
}

/* Writes one RGXCreateHWRTDataSet call: its values and the device addresses of its arrays. */
static void
shim_hwrt(
	FILE *file,
	unsigned long number,
	const struct shim_srvkm_cmd *cmd)
{
	static const char *const array_names[6] = { "macrotile_array", "pm_mlist", "rtc", "rgn_header", "tail_ptrs", "vheap_table" };
	struct shim_hwrt_in in;
	uint64_t arrays[6];
	uint64_t entries[SHIM_ARRAY_ENTRIES];
	uint32_t array;
	uint32_t entry;
	size_t got;

	/* The header; a structure of another size is a layout this shim does not know. */
	fprintf(file, "== hwrt %lu pid %ld in_size %u out_size %u\n", number, (long)getpid(), (unsigned)cmd->in_data_size, (unsigned)cmd->out_data_size);
	if (cmd->in_data_size != sizeof(in)) {
		fprintf(file, "hwrt: in_size is not %lu, raw words follow\n", (unsigned long)sizeof(in));
		shim_words(file, "in", cmd->in_data_ptr, cmd->in_data_size);
		return;
	}

	/* The in structure. */
	memset(&in, 0, sizeof(in));
	got = shim_read(cmd->in_data_ptr, &in, sizeof(in));
	if (got != sizeof(in)) {
		fprintf(file, "hwrt: in structure unreadable\n");
		return;
	}

	/* Its values. */
	fprintf(file, "multi_sample_ctl 0x%016llx flipped 0x%016llx max_rts %u\n", (unsigned long long)in.multi_sample_ctl,
		(unsigned long long)in.flipped_multi_sample_ctl, (unsigned)in.max_rts);
	fprintf(file, "isp_merge lower %u,%u upper %u,%u scale 0x%08x,0x%08x isp_mtile_size 0x%08x mtile_stride %u\n", (unsigned)in.isp_merge_lower_x,
		(unsigned)in.isp_merge_lower_y, (unsigned)in.isp_merge_upper_x, (unsigned)in.isp_merge_upper_y, (unsigned)in.isp_merge_scale_x,
		(unsigned)in.isp_merge_scale_y, (unsigned)in.isp_mtile_size, (unsigned)in.mtile_stride);
	fprintf(file, "ppp_screen 0x%08x te_screen 0x%08x te_aa 0x%08x te_mtile1 0x%08x te_mtile2 0x%08x\n", (unsigned)in.ppp_screen,
		(unsigned)in.te_screen, (unsigned)in.te_aa, (unsigned)in.te_mtile1, (unsigned)in.te_mtile2);
	fprintf(file, "rgn_header_size %u tpc_size %u tpc_stride %u\n", (unsigned)in.rgn_header_size, (unsigned)in.tpc_size, (unsigned)in.tpc_stride);

	/* The arrays' device addresses, as many as can be read up to SHIM_ARRAY_ENTRIES. */
	arrays[0] = in.macrotile_array;
	arrays[1] = in.pm_mlist;
	arrays[2] = in.rtc;
	arrays[3] = in.rgn_header;
	arrays[4] = in.tail_ptrs;
	arrays[5] = in.vheap_table;
	for (array = 0; array < 6U; array++) {
		memset(entries, 0, sizeof(entries));
		got = shim_read(arrays[array], entries, sizeof(entries));
		fprintf(file, "%s:", array_names[array]);
		for (entry = 0; entry < got / sizeof(entries[0]); entry++)
			fprintf(file, " 0x%010llx", (unsigned long long)entries[entry]);
		fprintf(file, "\n");
	}
}

/* Writes a bridge call of the two kinds when it is among the ones asked for. */
static void
shim_record(
	const struct shim_srvkm_cmd *argument)
{
	struct shim_srvkm_cmd cmd;
	unsigned long number;
	FILE *file;

	/* The ioctl's own structure; only the RGXTA3D group's two calls. */
	memcpy(&cmd, argument, sizeof(cmd));
	if (cmd.bridge_id != SHIM_BRIDGE_RGXTA3D)
		return;
	if (cmd.bridge_func_id != SHIM_FUNC_KICKTA3D2 && cmd.bridge_func_id != SHIM_FUNC_HWRTDATASET)
		return;

	pthread_mutex_lock(&shim_lock);

	/* The call's number of its kind; outside the window, nothing. */
	if (cmd.bridge_func_id == SHIM_FUNC_KICKTA3D2) {
		number = shim_kicks_seen++;
	} else {
		number = shim_hwrts_seen++;
	}
	if (number < shim_skip || number >= shim_skip + shim_max) {
		pthread_mutex_unlock(&shim_lock);
		return;
	}

	/* The record, appended. */
	file = fopen(shim_out, "a");
	if (file != NULL) {
		if (cmd.bridge_func_id == SHIM_FUNC_KICKTA3D2) {
			shim_kick(file, number, &cmd);
		} else {
			shim_hwrt(file, number, &cmd);
		}
		fclose(file);
	}

	pthread_mutex_unlock(&shim_lock);
}
