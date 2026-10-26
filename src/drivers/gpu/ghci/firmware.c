/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The command processor's firmware from the driver's side: the handshake
 * (GHCI_INIT) after every start of the firmware, the diagnostics root runs
 * on demand, and the reload of the firmware's image.
 *
 * INIT goes through the ring like any packet, so its reply proves the
 * whole path a packet takes: the CP reads the ring in host memory, the
 * firmware reads the packet's words and writes the reply into host
 * memory, the CP writes the fence after it.  A start whose INIT is not
 * answered, or not answered right, fails.
 *
 * The diagnostics take that path apart (docs/ghci.md section 7.2): the
 * register window, the debug port's single words of host and local
 * memory, the firmware's echo, a pattern the firmware writes into host
 * memory and the CPU reads, a pattern the CPU writes and the firmware
 * sums, and a pattern the firmware writes into local memory and sums.
 * Host memory is also read and written through a second view, the one
 * the driver does not use (the cached direct map beside an uncached view,
 * an uncached view beside the direct map), with no cache operation, so a
 * difference between the two shows where the GPU's accesses land (in
 * memory, or in a cache the uncached view does not see).  Every step is
 * logged.
 *
 * A reload stages the image (IMEM words, then DMEM words) in host memory
 * sent by root in pieces, resets the processor, loads the image with the
 * CPFW capability (from host memory, or from local memory after a copy
 * through the debug port) and starts the firmware again.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/clock.h>
#include <kern/cred.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "ghci.h"
#include "frame.h"
#include "hcr.h"
#include "packets.h"

/* Where the replies of INIT and of the diagnostics are in the firmware page. */
#define INIT_REPLY_OFFSET		0U
#define DIAG_REPLY_OFFSET		64U

/* The word a reply starts as, so a word the firmware did not write shows. */
#define REPLY_PENDING			0xdeadbeefU

/* VK_ERROR_FEATURE_NOT_PRESENT: a firmware's answer to a gpu-op it does not know. */
#define VK_FEATURE_NOT_PRESENT		(-8)

/* The first firmware version that answers GHCI_INIT (Amaterrace's CPFW_VERSION). */
#define INIT_FIRST_VERSION		0x00020001U

/* How long INIT and a diagnostic may take to their fence. */
#define INIT_WAIT_MS			1000U
#define DIAG_WAIT_MS			2000U

/* The diagnostics' buffers, the bytes of a pattern, the most rounds, the failures shown per step. */
#define DIAG_HOST_BYTES			65536U
#define DIAG_LOCAL_BYTES		65536U
#define DIAG_PATTERN_BYTES		16384U
#define DIAG_PATTERN_WORDS		(DIAG_PATTERN_BYTES / 4U)
#define DIAG_ROUNDS_MAX			100000U
#define DIAG_SHOWN			4U

/* Where in the host buffer the debug port's words are, past the firmware's pattern. */
#define DIAG_DBG_OFFSET			(DIAG_HOST_BYTES - 64U)

/* How long the debug port may stay busy: reads first, then sleeps of a millisecond. */
#define DBG_SPINS			10000U
#define DBG_WAIT_MS			100U

/* The words of the processor's memories (KC705: 128 KiB and 64 KiB), and where DMEM's image is staged. */
#define IMAGE_IMEM_WORDS		32768U
#define IMAGE_DMEM_WORDS		16384U
#define IMAGE_DMEM_OFFSET		(IMAGE_IMEM_WORDS * 4U)
#define IMAGE_BYTES			((IMAGE_IMEM_WORDS + IMAGE_DMEM_WORDS) * 4U)

/* How long a load of the image may take. */
#define IMAGE_LOAD_WAIT_MS		2000U

/* GPU_OP_GHCI_FIRMWARE: the head's words, its parts and its flags. */
#define FIRMWARE_HEAD_WORDS		5U
#define FIRMWARE_PART_IMEM		0U
#define FIRMWARE_PART_DMEM		1U
#define FIRMWARE_FLAG_LOAD		0x1U
#define FIRMWARE_FLAG_VIA_LOCAL		0x2U

/* GPU_OP_GHCI_DIAG: the words of the command, and its flag that paints the idle frame instead. */
#define DIAG_COMMAND_WORDS		3U
#define DIAG_FLAG_PAINT			0x1U
#define DIAG_FLAG_REPORT		0x2U

/* The idle frame's test picture: colour bands over the top two thirds, grey steps below. */
#define PAINT_BANDS			8U
#define PAINT_STEPS			16U

/* The words before a packet's arguments: the header, the gpu-op, the flags, the reply's address. */
#define PACKET_HEAD_DWORDS		4U

/* The most argument words of one packet of this file. */
#define PACKET_ARGUMENTS_MAX		15U

/* The CPFW capability's registers, from its first (CPFW_CTRL). */
#define CPFW_STATUS_OFFSET		(GHCI_CPFW_STATUS - GHCI_CPFW_CTRL)
#define CPFW_IBASE_OFFSET		(GHCI_CPFW_IBASE - GHCI_CPFW_CTRL)
#define CPFW_IWORDS_OFFSET		(GHCI_CPFW_IWORDS - GHCI_CPFW_CTRL)
#define CPFW_DBASE_OFFSET		(GHCI_CPFW_DBASE - GHCI_CPFW_CTRL)
#define CPFW_DWORDS_OFFSET		(GHCI_CPFW_DWORDS - GHCI_CPFW_CTRL)
#define CPFW_VERSION_OFFSET		(GHCI_CPFW_VERSION - GHCI_CPFW_CTRL)

/*
 * The steps of a round of the diagnostics.  The _OTHER steps reach host
 * memory through the view the driver does not use (struct diag_run).
 */
enum diag_step {
	DIAG_REGISTER = 0,
	DIAG_DBG_HOST_READ,
	DIAG_DBG_HOST_WRITE,
	DIAG_DBG_HOST_WRITE_OTHER,
	DIAG_DBG_LOCAL,
	DIAG_ECHO,
	DIAG_FW_HOST_WRITE,
	DIAG_FW_HOST_WRITE_OTHER,
	DIAG_FW_HOST_SUM,
	DIAG_FW_HOST_SUM_OTHER,
	DIAG_FW_LOCAL,
	DIAG_STEPS
};

/*
 * One run of the diagnostics: the device, the buffers and their views,
 * the seed of the patterns and what each step counted.
 *
 * host is the driver's view of the host buffer; other the view it does
 * not use: the cached direct map beside an uncached view, an uncached
 * view (device->diag_alias) beside the direct map, NULL where the kernel
 * has no uncached views; other_cached says other is the direct map.  local_address is 0 on a GPU without local memory.  The run
 * lives on the caller's stack for one call of ghci_firmware_diag().
 */
struct diag_run {
	struct ghci_device *device;
	volatile uint32_t *host;
	volatile uint32_t *other;
	int other_cached;
	uint32_t host_address;
	uint32_t local_address;
	uint32_t seed;
	unsigned round;
	unsigned passed[DIAG_STEPS];
	unsigned failed[DIAG_STEPS];
};

/*
 * The names of the steps in the log, in the order of enum diag_step.
 */
static const char *const diag_step_names[DIAG_STEPS] = {
	"register",
	"dbg-host-read",
	"dbg-host-write",
	"dbg-host-write-other",
	"dbg-local",
	"fw-echo",
	"fw-host-write",
	"fw-host-write-other",
	"fw-host-sum",
	"fw-host-sum-other",
	"fw-local"
};

/*
 * The cache operations of the HAL.
 *
 * Only architectures whose HAL maintains the data cache build them; on
 * the others the CPU's view of host memory needs none, and a null test
 * skips them.
 */
extern void kern_dcache_clean_range(const void *address, size_t size) __attribute__((weak));
extern void kern_dcache_invalidate_range(const void *address, size_t size) __attribute__((weak));

/*
 * The uncached views of RAM.
 *
 * A HAL without them leaves them out of the kernel; the diagnostics then
 * have no second view beside the direct map.
 */
extern int kern_pmem_map_uncached(const struct kern_pmem *run, void **mapped) __attribute__((weak));
extern int kern_pmem_unmap_uncached(void *mapped, size_t size) __attribute__((weak));

static int packet_run(struct ghci_device *device, uint32_t offset, uint32_t opcode, const uint32_t *arguments, uint32_t count, uint32_t *reply, unsigned milliseconds);
static void log_state(struct ghci_device *device, const char *what, uint32_t fence);
static int cpfw_registers(struct ghci_device *device, uint32_t *registers);
static uint32_t cpfw_version(struct ghci_device *device);
static void cache_invalidate(volatile void *address, uint32_t bytes);
static void cache_clean(volatile void *address, uint32_t bytes);
static void view_invalidate(struct ghci_device *device, volatile void *address, uint32_t bytes);
static void view_clean(struct ghci_device *device, volatile void *address, uint32_t bytes);
static int dbg_wait(struct ghci_device *device);
static int dbg_read(struct ghci_device *device, uint32_t address, uint32_t *value);
static int dbg_write(struct ghci_device *device, uint32_t address, uint32_t value);
static int diag_buffers(struct ghci_device *device);
static int diag_round(struct diag_run *run);
static void diag_count(struct diag_run *run, enum diag_step step, int passed);
static int diag_shown(const struct diag_run *run, enum diag_step step);
static uint32_t diag_random(struct diag_run *run);
static void diag_register(struct diag_run *run);
static void diag_dbg_host(struct diag_run *run);
static void diag_dbg_local(struct diag_run *run);
static int diag_echo(struct diag_run *run);
static int diag_fw_write(struct diag_run *run);
static int diag_fw_sum(struct diag_run *run, int other);
static int diag_fw_local(struct diag_run *run);
static unsigned diag_compare(struct diag_run *run, enum diag_step step, volatile uint32_t *view, uint32_t pattern, uint32_t step_value);
static void diag_sums(uint32_t pattern, uint32_t step_value, uint32_t words, uint32_t *plain, uint32_t *weighted);
static void diag_summary(const struct diag_run *run);
static int firmware_stage(struct ghci_device *device, const uint32_t *words, uint32_t count);
static int firmware_reload(struct ghci_device *device, uint32_t flags);
static int image_to_local(struct ghci_device *device, uint32_t local);
static int paint_fill(struct ghci_device *device, uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
static uint32_t paint_color(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
static int paint_check(struct ghci_device *device, uint32_t base, uint32_t stride, uint32_t width, uint32_t height);

/*
 * The colours of the bands, left to right (BGRA8 words: 0x00RRGGBB):
 * white, yellow, cyan, green, magenta, red, blue, black.
 */
static const uint32_t paint_bands[PAINT_BANDS] = {
	0x00ffffffU,
	0x00ffff00U,
	0x0000ffffU,
	0x0000ff00U,
	0x00ff00ffU,
	0x00ff0000U,
	0x000000ffU,
	0x00000000U
};

/*
 * Sends GHCI_INIT and checks the firmware's reply.
 *
 * A GPU without firmware has nothing to answer.  A firmware older than
 * GHCI_INIT answers it as a gpu-op it does not know; that answer still
 * came through the ring and host memory, so the device starts, without
 * the diagnostics, and the log asks for a newer firmware.
 */
int
ghci_firmware_init(
	struct ghci_device *device)
{
	uint32_t reply[GHCI_REPLY_WORDS];
	uint32_t arguments[2];
	uint32_t token;
	uint32_t version;
	uint32_t index;
	int wrong;
	int error;

	/* A command processor without firmware decodes no gpu-op. */
	device->fw_features = 0U;
	if ((device->caps & GHCI_CAPS_FIRMWARE) == 0U) {
		kern_logf("ghci: %s: firmware init: no firmware (CAPS[17] 0), nothing to ask\n", device->name);
		return 0;
	}

	/* A token of its own for every INIT, so an old reply is never taken for this one. */
	device->fw_tokens++;
	token = 0x494e0000U ^ (device->fw_tokens * 0x9e3779b9U) ^ (uint32_t)clock_ticks();
	arguments[0] = token;
	arguments[1] = GHCI_INIT_PROTOCOL;
	kern_logf("ghci: %s: firmware init: token %08x, reply at %08x\n",
	    device->name,
	    (unsigned)token,
	    (unsigned)(device->fw_page->gpu_address + INIT_REPLY_OFFSET));

	/* The packet, its fence and the reply. */
	error = packet_run(device, INIT_REPLY_OFFSET, GHCI_GPU_OP_INIT, arguments, 2U, reply, INIT_WAIT_MS);
	if (error != 0) {
		kern_logf("ghci: %s: firmware init: no answer (%d)\n", device->name, error);
		return error;
	}

	/* An older firmware: the answer to a gpu-op it does not know. */
	version = cpfw_version(device);
	if ((int32_t)reply[0] == VK_FEATURE_NOT_PRESENT &&
	    reply[1] == 0U &&
	    version < INIT_FIRST_VERSION) {
		device->fw_version = version;
		kern_logf("ghci: %s: firmware init: firmware %08x predates INIT; answered FEATURE_NOT_PRESENT through the ring and host memory, "
		    "but has no diagnostics: load a newer one (ghcictl fwload)\n",
		    device->name,
		    (unsigned)version);
		return 0;
	}

	/* The answer must be a success with the token, its complement and a protocol of the same major. */
	wrong = 0;
	if (reply[0] != 0U) {
		wrong = 1;
	} else if (reply[1] != token) {
		wrong = 1;
	} else if (reply[GHCI_INIT_NOT_TOKEN] != ~token) {
		wrong = 1;
	} else if ((reply[GHCI_INIT_PROTOCOL_WORD] >> 16) != (GHCI_INIT_PROTOCOL >> 16)) {
		wrong = 1;
	}

	/* A wrong answer is shown word by word, and the start fails. */
	if (wrong) {
		kern_logf("ghci: %s: firmware init: wrong answer (token %08x, CPFW_VERSION %08x):\n",
		    device->name,
		    (unsigned)token,
		    (unsigned)version);
		for (index = 0U; index < GHCI_REPLY_WORDS; index += 8U) {
			kern_logf("ghci: %s:   reply[%u..] %08x %08x %08x %08x %08x %08x %08x %08x\n",
			    device->name,
			    (unsigned)index,
			    (unsigned)reply[index],
			    (unsigned)reply[index + 1U],
			    (unsigned)reply[index + 2U],
			    (unsigned)reply[index + 3U],
			    (unsigned)reply[index + 4U],
			    (unsigned)reply[index + 5U],
			    (unsigned)reply[index + 6U],
			    (unsigned)reply[index + 7U]);
		}
		return EIO;
	}

	/* The firmware that runs. */
	device->fw_version = reply[GHCI_INIT_VERSION];
	device->fw_features = reply[GHCI_INIT_FEATURES];
	kern_logf("ghci: %s: firmware init: firmware %u.%u, protocol %u.%u, features %x, line %u, sessions %u, packet read at %08x\n",
	    device->name,
	    (unsigned)(device->fw_version >> 16),
	    (unsigned)(device->fw_version & 0xffffU),
	    (unsigned)(reply[GHCI_INIT_PROTOCOL_WORD] >> 16),
	    (unsigned)(reply[GHCI_INIT_PROTOCOL_WORD] & 0xffffU),
	    (unsigned)device->fw_features,
	    (unsigned)reply[GHCI_INIT_LINE],
	    (unsigned)reply[GHCI_INIT_SESSIONS],
	    (unsigned)reply[GHCI_INIT_PACKET]);

	/* Succeeded: the firmware answers through the ring and host memory. */
	return 0;
}

/*
 * Runs the diagnostics a number of rounds and logs every step.
 *
 * Returns 0 when every step passed, EIO when one failed, ETIMEDOUT when a
 * packet was not answered (the run stops there), EBUSY while another run
 * goes on, or why the buffers could not be had.
 */
int
ghci_firmware_diag(
	struct ghci_device *device,
	unsigned rounds)
{
	struct diag_run run;
	unsigned failures;
	unsigned others;
	unsigned index;
	int busy;
	int error;

	/* At least one round, not too many. */
	if (rounds == 0U || rounds > DIAG_ROUNDS_MAX)
		return EINVAL;

	/* One run at a time; its buffers are kept until the detach. */
	mutex_lock(&device->lock);

	busy = device->diag_running;
	error = 0;
	if (!busy) {
		device->diag_running = 1;
		error = diag_buffers(device);
		if (error != 0)
			device->diag_running = 0;
	}

	mutex_unlock(&device->lock);

	/* Another run, or no memory for this one. */
	if (busy)
		return EBUSY;
	if (error != 0) {
		kern_logf("ghci: %s: diag: no buffers (%d)\n", device->name, error);
		return error;
	}

	/* The run's buffers and views. */
	memset(&run, 0, sizeof(run));
	run.device = device;
	run.host = (volatile uint32_t *)device->diag_host->view;
	run.host_address = device->diag_host->gpu_address;
	run.other = (volatile uint32_t *)device->diag_alias;
	run.other_cached = 0;
	if (device->host_uncached) {
		run.other = (volatile uint32_t *)kern_pmem_to_kernel(device->diag_host->physical);
		run.other_cached = 1;
	}
	run.local_address = 0U;
	if (device->diag_local != NULL)
		run.local_address = device->diag_local->gpu_address;
	run.seed = 0x2545f491U ^ (uint32_t)clock_ticks();
	kern_logf("ghci: %s: diag: %u rounds, host buffer %08x (%s view%s), local buffer %08x, firmware %08x features %x, seed %08x\n",
	    device->name,
	    rounds,
	    (unsigned)run.host_address,
	    device->host_uncached ? "uncached" : "cached",
	    run.other == NULL ? "" : (run.other_cached ? ", and the cached direct map" : ", and an uncached alias"),
	    (unsigned)run.local_address,
	    (unsigned)device->fw_version,
	    (unsigned)device->fw_features,
	    (unsigned)run.seed);

	/* The rounds; a packet that is not answered ends the run. */
	error = 0;
	for (index = 0U; index < rounds; index++) {
		run.round = index;
		error = diag_round(&run);
		if (error != 0)
			break;
	}

	/* What each step counted. */
	diag_summary(&run);

	/* The next run may begin. */
	mutex_lock(&device->lock);

	device->diag_running = 0;

	mutex_unlock(&device->lock);

	/* Reports a run that stopped, or a step that failed. */
	if (error != 0) {
		kern_logf("ghci: %s: diag: stopped in round %u (%d)\n", device->name, index, error);
		return error;
	}
	/*
	 * The verdict counts the driver's own view only.  The other view's
	 * steps say how the platform's caches meet the GPU's accesses (on the
	 * VisionFive 2 the GPU reads through the L2, so the uncached alias's
	 * writes are not seen): an observation, logged, not a failure.
	 */
	failures = 0U;
	others = 0U;
	for (index = 0U; index < DIAG_STEPS; index++) {
		if (index == DIAG_DBG_HOST_WRITE_OTHER ||
		    index == DIAG_FW_HOST_WRITE_OTHER ||
		    index == DIAG_FW_HOST_SUM_OTHER) {
			others += run.failed[index];
			continue;
		}
		failures += run.failed[index];
	}
	if (others != 0U)
		kern_logf("ghci: %s: diag: the other view disagrees with the GPU %u times (an observation of the platform, not a failure)\n", device->name, others);
	if (failures != 0U) {
		kern_logf("ghci: %s: diag: FAIL (%u failures)\n", device->name, failures);
		return EIO;
	}

	/* Succeeded: every step passed every round. */
	kern_logf("ghci: %s: diag: PASS\n", device->name);
	return 0;
}

/*
 * Paints the display's idle frame (local memory) with the blitter: colour
 * bands over the top two thirds and sixteen grey steps below, then has
 * the firmware sum the frame and compares the sums with the picture's.
 *
 * Returns 0 when the sums match (or the firmware has no DIAG_SUM: the
 * picture is only to be seen), EIO when they do not, ENOENT without an
 * idle frame, or why a blit failed.
 */
int
ghci_firmware_paint(
	struct ghci_device *device)
{
	uint32_t base;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t band;
	uint32_t step;
	uint32_t top;
	uint32_t grey;
	uint32_t left;
	uint32_t right;
	int shown;
	int error;

	/* The frame and its mode. */
	error = ghci_display_idle_frame(device, &base, &width, &height, &stride, &shown);
	if (error != 0) {
		kern_logf("ghci: %s: paint: no idle frame (%d)\n", device->name, error);
		return error;
	}
	kern_logf("ghci: %s: paint: the idle frame at %08x, %ux%u, stride %u, %s\n",
	    device->name,
	    (unsigned)base,
	    (unsigned)width,
	    (unsigned)height,
	    (unsigned)stride,
	    shown ? "scanned out now" : "not scanned out now");

	/* The whole frame black first, its rows' padding too. */
	error = paint_fill(device, base, stride, 0U, 0U, stride / 4U, height, 0U);
	if (error != 0)
		return error;

	/* The bands over the top two thirds. */
	top = height * 2U / 3U;
	for (band = 0U; band < PAINT_BANDS; band++) {
		left = band * width / PAINT_BANDS;
		right = (band + 1U) * width / PAINT_BANDS;
		error = paint_fill(device, base, stride, left, 0U, right - left, top, paint_bands[band]);
		if (error != 0)
			return error;
	}

	/* The grey steps below, black to white. */
	for (step = 0U; step < PAINT_STEPS; step++) {
		grey = step * 255U / (PAINT_STEPS - 1U);
		left = step * width / PAINT_STEPS;
		right = (step + 1U) * width / PAINT_STEPS;
		error = paint_fill(device, base, stride, left, top, right - left, height - top, grey << 16 | grey << 8 | grey);
		if (error != 0)
			return error;
	}
	kern_logf("ghci: %s: paint: %u bands and %u grey steps blitted\n", device->name, PAINT_BANDS, PAINT_STEPS);

	/* The firmware reads the frame back. */
	error = paint_check(device, base, stride, width, height);
	if (error != 0)
		return error;

	/* Succeeded: the picture is in the frame. */
	return 0;
}

/*
 * Logs the display's report and the firmware's sums of the first
 * megabyte of the idle frame (0 for a frame of zeros).
 */
int
ghci_firmware_report(
	struct ghci_device *device)
{
	uint32_t arguments[3];
	uint32_t reply[GHCI_REPLY_WORDS];
	uint32_t base;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t word;
	uint32_t rows[] = { 0U, 1U, 2U, 8U, 100U, 360U, 719U };
	unsigned index;
	int shown;
	int error;

	/* The controller and the transmitter. */
	ghci_display_report(device);

	/* The idle frame: the first word of some rows through the debug port. */
	error = ghci_display_idle_frame(device, &base, &width, &height, &stride, &shown);
	if (error != 0)
		return 0;
	for (index = 0U; index < sizeof(rows) / sizeof(rows[0]); index++) {
		word = 0U;
		error = dbg_read(device, base + rows[index] * stride, &word);
		kern_logf("ghci: %s: report: idle frame row %u word 0 at %08x: %08x (%d)\n",
		    device->name,
		    (unsigned)rows[index],
		    (unsigned)(base + rows[index] * stride),
		    (unsigned)word,
		    error);
	}

	/* Its first megabyte, summed by the firmware. */
	if ((device->fw_features & GHCI_INIT_F_DIAG) == 0U)
		return 0;
	arguments[0] = base;
	arguments[1] = 0U;
	arguments[2] = GHCI_DIAG_BYTES_MAX;
	error = packet_run(device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_SUM, arguments, 3U, reply, DIAG_WAIT_MS);
	if (error != 0)
		return error;
	kern_logf("ghci: %s: report: idle frame %08x (%s): first MiB result %08x sum %08x weighted %08x, words %08x %08x %08x %08x, last %08x\n",
	    device->name,
	    (unsigned)base,
	    shown ? "shown" : "not shown",
	    (unsigned)reply[0],
	    (unsigned)reply[GHCI_DIAG_SUM_PLAIN],
	    (unsigned)reply[GHCI_DIAG_SUM_WEIGHTED],
	    (unsigned)reply[GHCI_DIAG_SUM_FIRST],
	    (unsigned)reply[GHCI_DIAG_SUM_FIRST + 1U],
	    (unsigned)reply[GHCI_DIAG_SUM_FIRST + 2U],
	    (unsigned)reply[GHCI_DIAG_SUM_FIRST + 3U],
	    (unsigned)reply[GHCI_DIAG_SUM_LAST]);

	/* Succeeded: the report is in the log. */
	return 0;
}

/*
 * Takes GPU_OP_GHCI_DIAG or GPU_OP_GHCI_FIRMWARE from root.
 *
 * GPU_OP_GHCI_DIAG [number][flags 0][rounds] runs the diagnostics.
 * GPU_OP_GHCI_FIRMWARE [number][flags][part][offset][count][count words]
 * stages words of the image (part 0 IMEM, 1 DMEM, offset and count in
 * words); with FIRMWARE_FLAG_LOAD the staged image is then loaded and
 * the firmware started again (FIRMWARE_FLAG_VIA_LOCAL: from local
 * memory, copied there through the debug port).
 */
int
ghci_firmware_command(
	struct ghci_device *device,
	struct ghci_session *session,
	const void *stream,
	uint32_t bytes)
{
	const uint32_t *words;
	uint32_t count;
	uint32_t flags;
	int error;

	/* Whole words, at least a number and its flags. */
	words = stream;
	if ((bytes % 4U) != 0U)
		return EINVAL;
	count = bytes / 4U;
	if (count < 2U)
		return EINVAL;

	/* The diagnostics, or the idle frame's picture, which report in the log and by their error. */
	if (words[0] == GPU_OP_GHCI_DIAG) {
		if (count != DIAG_COMMAND_WORDS)
			return EINVAL;
		if (words[1] == DIAG_FLAG_PAINT) {
			error = ghci_firmware_paint(device);
		} else if (words[1] == DIAG_FLAG_REPORT) {
			error = ghci_firmware_report(device);
		} else if (words[1] == 0U) {
			error = ghci_firmware_diag(device, words[2]);
		} else {
			error = EINVAL;
		}
		if (error != 0)
			return error;

		/* Succeeded: every step passed, or the picture is in the frame. */
		return 0;
	}

	/* Otherwise a piece of the image, and perhaps the load. */
	if (count < FIRMWARE_HEAD_WORDS)
		return EINVAL;
	flags = words[1];
	if ((flags & ~(FIRMWARE_FLAG_LOAD | FIRMWARE_FLAG_VIA_LOCAL)) != 0U)
		return EINVAL;
	error = firmware_stage(device, words, count);
	if (error != 0)
		return error;

	/* A piece without the load is kept for the next. */
	if ((flags & FIRMWARE_FLAG_LOAD) == 0U)
		return 0;

	/* The load and the new start. */
	(void)session;
	error = firmware_reload(device, flags);
	if (error != 0)
		return error;

	/* Succeeded: the new firmware answers. */
	return 0;
}

/*
 * Loads the staged image into the processor's memories after a reset.
 *
 * The processor is reset and stopped (CPFW_CTRL.RUN 0): LOAD holds it in
 * reset, reads IMEM and DMEM from IBASE and DBASE, and sets READY.  The
 * caller then starts the firmware (ghci_firmware_start).
 */
int
ghci_firmware_load(
	struct ghci_device *device)
{
	uint32_t registers;
	uint32_t status;
	int error;

	/* The CPFW capability's registers; a GPU without one cannot reload. */
	error = cpfw_registers(device, &registers);
	if (error != 0) {
		kern_logf("ghci: %s: firmware load: no CPFW capability\n", device->name);
		return EIO;
	}

	/* Where the images are (host memory, or the copy in local memory) and their words, then LOAD (RUN stays 0: the core is held). */
	kern_logf("ghci: %s: firmware load: IMEM %u words at %08x, DMEM %u words at %08x\n",
	    device->name,
	    (unsigned)device->fw_iwords,
	    (unsigned)device->fw_ibase,
	    (unsigned)device->fw_dwords,
	    (unsigned)device->fw_dbase);
	ghci_write(device, registers + CPFW_IBASE_OFFSET, device->fw_ibase);
	ghci_write(device, registers + CPFW_IWORDS_OFFSET, device->fw_iwords);
	ghci_write(device, registers + CPFW_DBASE_OFFSET, device->fw_dbase);
	ghci_write(device, registers + CPFW_DWORDS_OFFSET, device->fw_dwords);
	ghci_write(device, registers, GHCI_CPFW_CTRL_LOAD);

	/* The load ends with READY and no FAULT, or it failed. */
	error = ghci_poll(
		device,
		registers + CPFW_STATUS_OFFSET,
		GHCI_CPFW_STATUS_LOADING | GHCI_CPFW_STATUS_READY | GHCI_CPFW_STATUS_FAULT,
		GHCI_CPFW_STATUS_READY,
		IMAGE_LOAD_WAIT_MS);
	status = ghci_read(device, registers + CPFW_STATUS_OFFSET);
	if (error != 0) {
		kern_logf("ghci: %s: firmware load: failed (CPFW_STATUS %08x)\n", device->name, (unsigned)status);
		return EIO;
	}

	/* Succeeded: the image is in the processor's memories. */
	kern_logf("ghci: %s: firmware load: done (CPFW_STATUS %08x)\n", device->name, (unsigned)status);
	return 0;
}

/*
 * Gives back the staged image and the diagnostics' buffers at the detach,
 * once the processor has stopped.
 */
void
ghci_firmware_release(
	struct ghci_device *device)
{
	/* The GPU uses none of them any more. */
	mutex_lock(&device->lock);

	if (device->fw_image != NULL)
		ghci_memory_free(device, device->fw_image);
	if (device->fw_image_local != NULL)
		ghci_memory_free(device, device->fw_image_local);
	if (device->diag_alias != NULL && kern_pmem_unmap_uncached != NULL)
		(void)kern_pmem_unmap_uncached(device->diag_alias, DIAG_HOST_BYTES);
	device->diag_alias = NULL;
	if (device->diag_host != NULL)
		ghci_memory_free(device, device->diag_host);
	if (device->diag_local != NULL)
		ghci_memory_free(device, device->diag_local);
	device->fw_image = NULL;
	device->fw_image_local = NULL;
	device->diag_host = NULL;
	device->diag_local = NULL;

	mutex_unlock(&device->lock);
}

/*
 * Sends one packet of this file (a reply of GHCI_REPLY_WORDS words at an
 * offset of the firmware page) with the driver's fence, waits for the
 * fence and copies the reply.
 */
static int
packet_run(
	struct ghci_device *device,
	uint32_t offset,
	uint32_t opcode,
	const uint32_t *arguments,
	uint32_t count,
	uint32_t *reply,
	unsigned milliseconds)
{
	uint32_t packet[PACKET_HEAD_DWORDS + PACKET_ARGUMENTS_MAX];
	struct ghci_ticket ticket;
	volatile uint32_t *slot;
	uint32_t index;
	int error;

	/* A few words of arguments only. */
	if (count > PACKET_ARGUMENTS_MAX)
		return EINVAL;

	/* The reply starts pending, so a word the firmware did not write shows. */
	slot = (volatile uint32_t *)(device->fw_page->view + offset);
	for (index = 0U; index < GHCI_REPLY_WORDS; index++)
		slot[index] = REPLY_PENDING;
	view_clean(device, slot, GHCI_REPLY_WORDS * 4U);
	kern_io_write_barrier();

	/* The packet: the driver's tag, the gpu-op, a result, the reply's address, the arguments. */
	packet[0] = GHCI_PKT_V(GHCI_TAG_DRIVER, PACKET_HEAD_DWORDS - 1U + count);
	packet[1] = opcode;
	packet[2] = GHCI_V_RESULT;
	packet[3] = device->fw_page->gpu_address + offset;
	for (index = 0U; index < count; index++)
		packet[PACKET_HEAD_DWORDS + index] = arguments[index];

	/* Into the ring with the fence after it. */
	error = ghci_cp_submit(device, NULL, packet, PACKET_HEAD_DWORDS + count, &ticket);
	if (error != 0) {
		kern_logf("ghci: %s: gpu-op %05x not written to the ring (%d)\n", device->name, (unsigned)opcode, error);
		return error;
	}

	/* The fence after the packet passes once the firmware is done with it. */
	error = ghci_cp_wait(device, &ticket, milliseconds);
	if (error != 0) {
		log_state(device, "no fence after the gpu-op", ticket.fence);
		return error;
	}

	/* The reply, read after the fence (and the cache dropped where the view is cached). */
	kern_io_read_barrier();
	view_invalidate(device, slot, GHCI_REPLY_WORDS * 4U);
	for (index = 0U; index < GHCI_REPLY_WORDS; index++)
		reply[index] = slot[index];

	/* Succeeded: the reply as the firmware left it. */
	return 0;
}

/* Logs where the ring, the fences and the firmware stand after a packet that was not answered. */
static void
log_state(
	struct ghci_device *device,
	const char *what,
	uint32_t fence)
{
	uint32_t registers;
	uint32_t firmware;
	uint32_t page;
	uint32_t page_cached;
	int error;

	/* The firmware's status, when the GPU has the CPFW capability. */
	firmware = 0U;
	error = cpfw_registers(device, &registers);
	if (error == 0)
		firmware = ghci_read(device, registers + CPFW_STATUS_OFFSET);

	/* The fence page's word, as the CPU sees it, and through the cached direct map beside an uncached view. */
	page = ((volatile uint32_t *)device->fence_page->view)[0];
	page_cached = page;
	if (device->host_uncached)
		page_cached = ((volatile uint32_t *)kern_pmem_to_kernel(device->fence_page->physical))[0];

	/* One line with all of it. */
	kern_logf("ghci: %s: %s: fence %08x; fence last %08x page %08x (direct map %08x), ring rptr %08x wptr %08x, "
	    "status %08x irq %08x, CPFW_STATUS %08x, fault %08x at %08x\n",
	    device->name,
	    what,
	    (unsigned)fence,
	    (unsigned)ghci_read(device, GHCI_FENCE_LAST),
	    (unsigned)page,
	    (unsigned)page_cached,
	    (unsigned)ghci_read(device, GHCI_RING_RPTR),
	    (unsigned)ghci_read(device, GHCI_RING_WPTR),
	    (unsigned)ghci_read(device, GHCI_STATUS),
	    (unsigned)ghci_read(device, GHCI_IRQ_STATUS),
	    (unsigned)firmware,
	    (unsigned)ghci_read(device, GHCI_FAULT_INFO),
	    (unsigned)ghci_read(device, GHCI_FAULT_ADDR));
}

/* Finds the CPFW capability's registers: 0, or ENOENT on a GPU without one. */
static int
cpfw_registers(
	struct ghci_device *device,
	uint32_t *registers)
{
	uint32_t version;
	int error;

	/* The extended capability list names it. */
	error = ghci_extcap_find(ghci_read_context, device, device->extcap, device->register_bytes, GHCI_XCAP_CPFW, registers, &version);
	if (error != 0)
		return ENOENT;

	/* Succeeded: the registers' offset. */
	return 0;
}

/* Reads CPFW_VERSION, which the firmware writes as it starts: 0 on a GPU without the CPFW capability. */
static uint32_t
cpfw_version(
	struct ghci_device *device)
{
	uint32_t registers;
	uint32_t version;
	int error;

	/* No capability, no version. */
	error = cpfw_registers(device, &registers);
	if (error != 0)
		return 0U;

	/* The firmware's own word. */
	version = ghci_read(device, registers + CPFW_VERSION_OFFSET);
	return version;
}

/* Drops the cached lines of host memory the GPU wrote, where the CPU's view is cached and the HAL has the operation. */
static void
cache_invalidate(
	volatile void *address,
	uint32_t bytes)
{
	/* The HAL of this architecture keeps the cache coherent itself. */
	if (kern_dcache_invalidate_range == NULL)
		return;

	/* The next read comes from memory. */
	kern_dcache_invalidate_range((const void *)address, bytes);
}

/* Writes back the cached lines of host memory the CPU wrote, where the HAL has the operation. */
static void
cache_clean(
	volatile void *address,
	uint32_t bytes)
{
	/* The HAL of this architecture keeps the cache coherent itself. */
	if (kern_dcache_clean_range == NULL)
		return;

	/* The GPU reads what the CPU wrote. */
	kern_dcache_clean_range((const void *)address, bytes);
}

/* Drops the cached lines of the driver's view of host memory, where that view is cached. */
static void
view_invalidate(
	struct ghci_device *device,
	volatile void *address,
	uint32_t bytes)
{
	/* An uncached view has no lines to drop. */
	if (device->host_uncached)
		return;

	/* The next read of the view comes from memory. */
	cache_invalidate(address, bytes);
}

/* Writes back the cached lines of the driver's view of host memory, where that view is cached. */
static void
view_clean(
	struct ghci_device *device,
	volatile void *address,
	uint32_t bytes)
{
	/* An uncached view's writes are in memory already. */
	if (device->host_uncached)
		return;

	/* The GPU reads what the CPU wrote. */
	cache_clean(address, bytes);
}

/* Waits until the debug port is idle: 0, ETIMEDOUT, or EIO when it refused the access. */
static int
dbg_wait(
	struct ghci_device *device)
{
	uint32_t status;
	unsigned spins;
	unsigned waited;

	/* An access takes a few microseconds: reads first. */
	status = GHCI_DBG_BUSY;
	for (spins = 0U; spins < DBG_SPINS; spins++) {
		status = ghci_read(device, GHCI_DBG_STATUS);
		if ((status & GHCI_DBG_BUSY) == 0U)
			break;
	}

	/* A slow one: a sleep of a millisecond between looks. */
	for (waited = 0U; (status & GHCI_DBG_BUSY) != 0U && waited < DBG_WAIT_MS; waited++) {
		ghci_sleep_ms(1U);
		status = ghci_read(device, GHCI_DBG_STATUS);
	}

	/* Still busy, or refused (outside the aperture, a bus error). */
	if ((status & GHCI_DBG_BUSY) != 0U)
		return ETIMEDOUT;
	if ((status & GHCI_DBG_ERROR) != 0U)
		return EIO;

	/* Succeeded: the access is done. */
	return 0;
}

/* Reads a word of GPU memory through the debug port. */
static int
dbg_read(
	struct ghci_device *device,
	uint32_t address,
	uint32_t *value)
{
	int error;

	/* Writing DBG_ADDR starts the read. */
	ghci_write(device, GHCI_DBG_ADDR, address);
	error = dbg_wait(device);
	if (error != 0)
		return error;

	/* Succeeded: the word. */
	*value = ghci_read(device, GHCI_DBG_DATA);
	return 0;
}

/* Writes a word of GPU memory through the debug port and waits for the write. */
static int
dbg_write(
	struct ghci_device *device,
	uint32_t address,
	uint32_t value)
{
	int error;

	/* DBG_ADDR first, which starts a read the write waits for. */
	ghci_write(device, GHCI_DBG_ADDR, address);
	error = dbg_wait(device);
	if (error != 0)
		return error;

	/* Writing DBG_DATA starts the write. */
	ghci_write(device, GHCI_DBG_DATA, value);
	error = dbg_wait(device);
	if (error != 0)
		return error;

	/* Succeeded: the word is in memory. */
	return 0;
}

/* Takes the diagnostics' buffers the first time: host memory and, on a GPU with it, local memory.  The device's lock is held. */
static int
diag_buffers(
	struct ghci_device *device)
{
	void *mapped;
	int error;

	/* The host buffer, which the CPU and the GPU both reach. */
	if (device->diag_host == NULL) {
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, DIAG_HOST_BYTES, &device->diag_host);
		if (error != 0)
			return error;
	}

	/* Beside the direct map, an uncached view of it, when the kernel has such views (none: no second view). */
	if (!device->host_uncached && device->diag_alias == NULL && kern_pmem_map_uncached != NULL) {
		mapped = NULL;
		error = kern_pmem_map_uncached(&device->diag_host->run, &mapped);
		if (error == 0)
			device->diag_alias = mapped;
	}

	/* The local buffer, which only the GPU reaches. */
	if (device->diag_local == NULL && device->local_size != 0U) {
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DEVICE, DIAG_LOCAL_BYTES, &device->diag_local);
		if (error != 0)
			return error;
	}

	/* Succeeded: the buffers are the device's until the detach. */
	return 0;
}

/* Runs one round of every step; a packet that is not answered ends it. */
static int
diag_round(
	struct diag_run *run)
{
	int error;

	/* The register window and the debug port, without the firmware. */
	diag_register(run);
	diag_dbg_host(run);
	diag_dbg_local(run);

	/* The firmware's steps need a firmware that has them. */
	if ((run->device->fw_features & GHCI_INIT_F_DIAG) == 0U)
		return 0;

	/* The echo of the packet's words. */
	error = diag_echo(run);
	if (error != 0)
		return error;

	/* The firmware writes host memory, the CPU reads it. */
	error = diag_fw_write(run);
	if (error != 0)
		return error;

	/* The CPU writes host memory, through its view and then through the other one, and the firmware sums it. */
	error = diag_fw_sum(run, 0);
	if (error != 0)
		return error;
	if (run->other != NULL) {
		error = diag_fw_sum(run, 1);
		if (error != 0)
			return error;
	}

	/* The firmware writes and sums local memory. */
	error = diag_fw_local(run);
	if (error != 0)
		return error;

	/* The direct map's lines go, so the next round's reads through it come from memory again. */
	if (run->other != NULL && run->other_cached)
		cache_invalidate(run->other, DIAG_HOST_BYTES);

	/* Succeeded: every step of the round ran. */
	return 0;
}

/* Counts a step's outcome. */
static void
diag_count(
	struct diag_run *run,
	enum diag_step step,
	int passed)
{
	/* One more pass or failure of the step. */
	if (passed) {
		run->passed[step]++;
	} else {
		run->failed[step]++;
	}
}

/* Reports whether a failure of a step is still shown in detail (the first few): 1 when it is. */
static int
diag_shown(
	const struct diag_run *run,
	enum diag_step step)
{
	/* Past the first few, only the counts. */
	if (run->failed[step] > DIAG_SHOWN)
		return 0;

	/* Succeeded: the failure is shown. */
	return 1;
}

/* Gives the next word of the run's patterns (xorshift32). */
static uint32_t
diag_random(
	struct diag_run *run)
{
	uint32_t word;

	/* One step of the generator; it never reaches 0. */
	word = run->seed;
	word ^= word << 13;
	word ^= word >> 17;
	word ^= word << 5;
	run->seed = word;

	/* The new word. */
	return word;
}

/* The register window: SCRATCH keeps what the CPU wrote. */
static void
diag_register(
	struct diag_run *run)
{
	uint32_t value;
	uint32_t read;
	int passed;

	/* A word written and read back. */
	value = diag_random(run);
	ghci_write(run->device, GHCI_SCRATCH, value);
	read = ghci_read(run->device, GHCI_SCRATCH);
	passed = 0;
	if (read == value)
		passed = 1;

	/* The outcome, and the words of a failure. */
	diag_count(run, DIAG_REGISTER, passed);
	if (!passed && diag_shown(run, DIAG_REGISTER)) {
		kern_logf("ghci: %s: diag: round %u: register: SCRATCH %08x, written %08x\n",
		    run->device->name,
		    run->round,
		    (unsigned)read,
		    (unsigned)value);
	}
}

/*
 * The debug port and host memory: the GPU reads a word the CPU wrote, and
 * writes one the CPU reads (through its view, and through the other
 * direct map where that is another view).
 */
static void
diag_dbg_host(
	struct diag_run *run)
{
	volatile uint32_t *word;
	uint32_t address;
	uint32_t value;
	uint32_t read;
	int passed;
	int error;

	/* The word the CPU writes, which the GPU reads. */
	word = &run->host[DIAG_DBG_OFFSET / 4U];
	address = run->host_address + DIAG_DBG_OFFSET;
	value = diag_random(run);
	*word = value;
	view_clean(run->device, word, 4U);
	kern_io_write_barrier();
	read = 0U;
	error = dbg_read(run->device, address, &read);
	passed = 0;
	if (error == 0 && read == value)
		passed = 1;
	diag_count(run, DIAG_DBG_HOST_READ, passed);
	if (!passed && diag_shown(run, DIAG_DBG_HOST_READ)) {
		kern_logf("ghci: %s: diag: round %u: dbg-host-read at %08x: %08x (%d), the CPU wrote %08x\n",
		    run->device->name,
		    run->round,
		    (unsigned)address,
		    (unsigned)read,
		    error,
		    (unsigned)value);
	}

	/* The word the GPU writes, which the CPU reads through its view. */
	word = &run->host[DIAG_DBG_OFFSET / 4U + 1U];
	address = run->host_address + DIAG_DBG_OFFSET + 4U;
	value = diag_random(run);
	*word = ~value;
	view_clean(run->device, word, 4U);
	kern_io_write_barrier();
	error = dbg_write(run->device, address, value);
	kern_io_read_barrier();
	view_invalidate(run->device, word, 4U);
	read = *word;
	passed = 0;
	if (error == 0 && read == value)
		passed = 1;
	diag_count(run, DIAG_DBG_HOST_WRITE, passed);
	if (!passed && diag_shown(run, DIAG_DBG_HOST_WRITE)) {
		kern_logf("ghci: %s: diag: round %u: dbg-host-write at %08x (%d): the CPU reads %08x, the GPU wrote %08x\n",
		    run->device->name,
		    run->round,
		    (unsigned)address,
		    error,
		    (unsigned)read,
		    (unsigned)value);
	}

	/* The same word through the other view, with no cache operation. */
	if (run->other == NULL || error != 0)
		return;
	read = run->other[DIAG_DBG_OFFSET / 4U + 1U];
	passed = 0;
	if (read == value)
		passed = 1;
	diag_count(run, DIAG_DBG_HOST_WRITE_OTHER, passed);
	if (!passed && diag_shown(run, DIAG_DBG_HOST_WRITE_OTHER)) {
		kern_logf("ghci: %s: diag: round %u: dbg-host-write-other at %08x: the other view reads %08x, the GPU wrote %08x\n",
		    run->device->name,
		    run->round,
		    (unsigned)address,
		    (unsigned)read,
		    (unsigned)value);
	}
}

/* The debug port and local memory: a word written and read back. */
static void
diag_dbg_local(
	struct diag_run *run)
{
	uint32_t address;
	uint32_t value;
	uint32_t read;
	int passed;
	int error;

	/* A GPU without local memory has no step here. */
	if (run->local_address == 0U)
		return;

	/* The last word of the local buffer, past the firmware's pattern. */
	address = run->local_address + DIAG_LOCAL_BYTES - 4U;
	value = diag_random(run);
	read = 0U;
	error = dbg_write(run->device, address, value);
	if (error == 0)
		error = dbg_read(run->device, address, &read);
	passed = 0;
	if (error == 0 && read == value)
		passed = 1;

	/* The outcome, and the words of a failure. */
	diag_count(run, DIAG_DBG_LOCAL, passed);
	if (!passed && diag_shown(run, DIAG_DBG_LOCAL)) {
		kern_logf("ghci: %s: diag: round %u: dbg-local at %08x (%d): read %08x, written %08x\n",
		    run->device->name,
		    run->round,
		    (unsigned)address,
		    error,
		    (unsigned)read,
		    (unsigned)value);
	}
}

/* The firmware's echo of fourteen words of the packet. */
static int
diag_echo(
	struct diag_run *run)
{
	uint32_t arguments[1U + GHCI_DIAG_ECHO_WORDS];
	uint32_t reply[GHCI_REPLY_WORDS];
	uint32_t index;
	uint32_t wrong;
	int passed;
	int error;

	/* The words to echo. */
	arguments[0] = GHCI_DIAG_ECHO_WORDS;
	for (index = 0U; index < GHCI_DIAG_ECHO_WORDS; index++)
		arguments[1U + index] = diag_random(run);

	/* The packet and its reply. */
	error = packet_run(run->device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_ECHO, arguments, 1U + GHCI_DIAG_ECHO_WORDS, reply, DIAG_WAIT_MS);
	if (error != 0)
		return error;

	/* The reply: success, the count, and each word as it was sent. */
	wrong = 0U;
	for (index = 0U; index < GHCI_DIAG_ECHO_WORDS; index++) {
		if (reply[2U + index] != arguments[1U + index])
			wrong++;
	}
	passed = 0;
	if (reply[0] == 0U && reply[1] == GHCI_DIAG_ECHO_WORDS && wrong == 0U)
		passed = 1;

	/* The outcome, and the first words of a failure. */
	diag_count(run, DIAG_ECHO, passed);
	if (!passed && diag_shown(run, DIAG_ECHO)) {
		kern_logf("ghci: %s: diag: round %u: fw-echo: result %08x count %08x, %u words differ; first %08x %08x, sent %08x %08x\n",
		    run->device->name,
		    run->round,
		    (unsigned)reply[0],
		    (unsigned)reply[1],
		    (unsigned)wrong,
		    (unsigned)reply[2],
		    (unsigned)reply[3],
		    (unsigned)arguments[1],
		    (unsigned)arguments[2]);
	}

	/* Succeeded: the packet was answered. */
	return 0;
}

/*
 * The firmware writes a pattern into host memory; the CPU reads it
 * through its view, and through the other view where there is one
 * another view.
 */
static int
diag_fw_write(
	struct diag_run *run)
{
	uint32_t arguments[5];
	uint32_t reply[GHCI_REPLY_WORDS];
	uint32_t pattern;
	uint32_t step_value;
	uint32_t index;
	unsigned wrong;
	int passed;
	int error;

	/* The buffer starts as the complement of the pattern, so an unwritten word shows. */
	pattern = diag_random(run);
	step_value = diag_random(run) | 1U;
	for (index = 0U; index < DIAG_PATTERN_WORDS; index++)
		run->host[index] = ~(pattern + index * step_value);
	view_clean(run->device, run->host, DIAG_PATTERN_BYTES);
	kern_io_write_barrier();

	/* DIAG_WRITE of the pattern. */
	arguments[0] = run->host_address;
	arguments[1] = 0U;
	arguments[2] = DIAG_PATTERN_BYTES;
	arguments[3] = pattern;
	arguments[4] = step_value;
	error = packet_run(run->device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_WRITE, arguments, 5U, reply, DIAG_WAIT_MS);
	if (error != 0)
		return error;

	/* The reply says every byte was written. */
	if (reply[0] != 0U || reply[1] != DIAG_PATTERN_BYTES) {
		diag_count(run, DIAG_FW_HOST_WRITE, 0);
		if (diag_shown(run, DIAG_FW_HOST_WRITE)) {
			kern_logf("ghci: %s: diag: round %u: fw-host-write at %08x: result %08x, %08x bytes written\n",
			    run->device->name,
			    run->round,
			    (unsigned)run->host_address,
			    (unsigned)reply[0],
			    (unsigned)reply[1]);
		}
		return 0;
	}

	/* The CPU's view, with the cache dropped where it is cached. */
	kern_io_read_barrier();
	view_invalidate(run->device, run->host, DIAG_PATTERN_BYTES);
	wrong = diag_compare(run, DIAG_FW_HOST_WRITE, run->host, pattern, step_value);
	passed = 0;
	if (wrong == 0U)
		passed = 1;
	diag_count(run, DIAG_FW_HOST_WRITE, passed);

	/* The other view, with no cache operation. */
	if (run->other == NULL)
		return 0;
	wrong = diag_compare(run, DIAG_FW_HOST_WRITE_OTHER, run->other, pattern, step_value);
	passed = 0;
	if (wrong == 0U)
		passed = 1;
	diag_count(run, DIAG_FW_HOST_WRITE_OTHER, passed);

	/* Succeeded: the packet was answered. */
	return 0;
}

/*
 * The CPU writes a pattern into host memory (through its view, or with
 * other nonzero through the other view with no cache operation) and the
 * firmware sums it.
 */
static int
diag_fw_sum(
	struct diag_run *run,
	int other)
{
	uint32_t arguments[3];
	uint32_t reply[GHCI_REPLY_WORDS];
	volatile uint32_t *view;
	enum diag_step step;
	uint32_t pattern;
	uint32_t step_value;
	uint32_t index;
	uint32_t plain;
	uint32_t weighted;
	int passed;
	int error;

	/* The view the CPU writes through, and the step it counts as. */
	view = run->host;
	step = DIAG_FW_HOST_SUM;
	if (other) {
		view = run->other;
		step = DIAG_FW_HOST_SUM_OTHER;
	}

	/* The pattern, made visible to the GPU (a clean only on the driver's view). */
	pattern = diag_random(run);
	step_value = diag_random(run);
	for (index = 0U; index < DIAG_PATTERN_WORDS; index++)
		view[index] = pattern + index * step_value;
	if (!other)
		view_clean(run->device, view, DIAG_PATTERN_BYTES);
	kern_io_write_barrier();

	/* DIAG_SUM of it. */
	arguments[0] = run->host_address;
	arguments[1] = 0U;
	arguments[2] = DIAG_PATTERN_BYTES;
	error = packet_run(run->device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_SUM, arguments, 3U, reply, DIAG_WAIT_MS);
	if (error != 0)
		return error;

	/* The sums the CPU works out, against the firmware's. */
	diag_sums(pattern, step_value, DIAG_PATTERN_WORDS, &plain, &weighted);
	passed = 0;
	if (reply[0] == 0U &&
	    reply[1] == DIAG_PATTERN_BYTES &&
	    reply[GHCI_DIAG_SUM_PLAIN] == plain &&
	    reply[GHCI_DIAG_SUM_WEIGHTED] == weighted)
		passed = 1;

	/* The outcome, and the words of a failure. */
	diag_count(run, step, passed);
	if (!passed && diag_shown(run, step)) {
		kern_logf("ghci: %s: diag: round %u: %s at %08x: result %08x bytes %08x, sum %08x weighted %08x "
		    "(the CPU's %08x %08x), first %08x %08x last %08x (the CPU wrote %08x %08x last %08x)\n",
		    run->device->name,
		    run->round,
		    diag_step_names[step],
		    (unsigned)run->host_address,
		    (unsigned)reply[0],
		    (unsigned)reply[1],
		    (unsigned)reply[GHCI_DIAG_SUM_PLAIN],
		    (unsigned)reply[GHCI_DIAG_SUM_WEIGHTED],
		    (unsigned)plain,
		    (unsigned)weighted,
		    (unsigned)reply[GHCI_DIAG_SUM_FIRST],
		    (unsigned)reply[GHCI_DIAG_SUM_FIRST + 1U],
		    (unsigned)reply[GHCI_DIAG_SUM_LAST],
		    (unsigned)pattern,
		    (unsigned)(pattern + step_value),
		    (unsigned)(pattern + (DIAG_PATTERN_WORDS - 1U) * step_value));
	}

	/* Succeeded: the packet was answered. */
	return 0;
}

/* The firmware writes a pattern into local memory and sums it again. */
static int
diag_fw_local(
	struct diag_run *run)
{
	uint32_t arguments[5];
	uint32_t write_reply[GHCI_REPLY_WORDS];
	uint32_t sum_reply[GHCI_REPLY_WORDS];
	uint32_t pattern;
	uint32_t step_value;
	uint32_t plain;
	uint32_t weighted;
	int passed;
	int error;

	/* A GPU without local memory has no step here. */
	if (run->local_address == 0U)
		return 0;

	/* DIAG_WRITE of a pattern. */
	pattern = diag_random(run);
	step_value = diag_random(run);
	arguments[0] = run->local_address;
	arguments[1] = 0U;
	arguments[2] = DIAG_PATTERN_BYTES;
	arguments[3] = pattern;
	arguments[4] = step_value;
	error = packet_run(run->device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_WRITE, arguments, 5U, write_reply, DIAG_WAIT_MS);
	if (error != 0)
		return error;

	/* DIAG_SUM of the same range. */
	error = packet_run(run->device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_SUM, arguments, 3U, sum_reply, DIAG_WAIT_MS);
	if (error != 0)
		return error;

	/* Both succeeded over every byte, and the sums are the pattern's. */
	diag_sums(pattern, step_value, DIAG_PATTERN_WORDS, &plain, &weighted);
	passed = 0;
	if (write_reply[0] == 0U &&
	    write_reply[1] == DIAG_PATTERN_BYTES &&
	    sum_reply[0] == 0U &&
	    sum_reply[1] == DIAG_PATTERN_BYTES &&
	    sum_reply[GHCI_DIAG_SUM_PLAIN] == plain &&
	    sum_reply[GHCI_DIAG_SUM_WEIGHTED] == weighted)
		passed = 1;

	/* The outcome, and the words of a failure. */
	diag_count(run, DIAG_FW_LOCAL, passed);
	if (!passed && diag_shown(run, DIAG_FW_LOCAL)) {
		kern_logf("ghci: %s: diag: round %u: fw-local at %08x: write %08x %08x, sum %08x %08x: %08x %08x "
		    "(the pattern's %08x %08x), first %08x last %08x (written %08x last %08x)\n",
		    run->device->name,
		    run->round,
		    (unsigned)run->local_address,
		    (unsigned)write_reply[0],
		    (unsigned)write_reply[1],
		    (unsigned)sum_reply[0],
		    (unsigned)sum_reply[1],
		    (unsigned)sum_reply[GHCI_DIAG_SUM_PLAIN],
		    (unsigned)sum_reply[GHCI_DIAG_SUM_WEIGHTED],
		    (unsigned)plain,
		    (unsigned)weighted,
		    (unsigned)sum_reply[GHCI_DIAG_SUM_FIRST],
		    (unsigned)sum_reply[GHCI_DIAG_SUM_LAST],
		    (unsigned)pattern,
		    (unsigned)(pattern + (DIAG_PATTERN_WORDS - 1U) * step_value));
	}

	/* Succeeded: the packets were answered. */
	return 0;
}

/* Counts the words of a view that differ from the pattern, and shows the first of a failure. */
static unsigned
diag_compare(
	struct diag_run *run,
	enum diag_step step,
	volatile uint32_t *view,
	uint32_t pattern,
	uint32_t step_value)
{
	uint32_t index;
	uint32_t word;
	uint32_t first;
	uint32_t first_word;
	unsigned wrong;

	/* Each word against the pattern; the first that differs is kept. */
	wrong = 0U;
	first = 0U;
	first_word = 0U;
	for (index = 0U; index < DIAG_PATTERN_WORDS; index++) {
		word = view[index];
		if (word == pattern + index * step_value)
			continue;
		if (wrong == 0U) {
			first = index;
			first_word = word;
		}
		wrong++;
	}

	/* A failure that is still shown in detail (diag_count comes after this). */
	if (wrong != 0U && run->failed[step] < DIAG_SHOWN) {
		kern_logf("ghci: %s: diag: round %u: %s: %u of %u words differ; the first at +%05x reads %08x, the pattern %08x\n",
		    run->device->name,
		    run->round,
		    diag_step_names[step],
		    wrong,
		    (unsigned)DIAG_PATTERN_WORDS,
		    (unsigned)(first * 4U),
		    (unsigned)first_word,
		    (unsigned)(pattern + first * step_value));
	}

	/* The count of words that differ. */
	return wrong;
}

/* Works out the sums DIAG_SUM gives for a pattern: the plain and the one weighted by place. */
static void
diag_sums(
	uint32_t pattern,
	uint32_t step_value,
	uint32_t words,
	uint32_t *plain,
	uint32_t *weighted)
{
	uint32_t index;
	uint32_t word;

	/* Word i is pattern + i x step; its weight is i + 1. */
	*plain = 0U;
	*weighted = 0U;
	for (index = 0U; index < words; index++) {
		word = pattern + index * step_value;
		*plain += word;
		*weighted += (index + 1U) * word;
	}
}

/* Logs what each step counted. */
static void
diag_summary(
	const struct diag_run *run)
{
	unsigned index;

	/* One line per step; a step that never ran says so. */
	for (index = 0U; index < DIAG_STEPS; index++) {
		if (run->passed[index] == 0U && run->failed[index] == 0U) {
			kern_logf("ghci: %s: diag: %s not run\n", run->device->name, diag_step_names[index]);
			continue;
		}
		kern_logf("ghci: %s: diag: %s %u passed, %u failed\n",
		    run->device->name,
		    diag_step_names[index],
		    run->passed[index],
		    run->failed[index]);
	}
}

/* Stages words of the image: GPU_OP_GHCI_FIRMWARE's part, offset and words. */
static int
firmware_stage(
	struct ghci_device *device,
	const uint32_t *words,
	uint32_t count)
{
	volatile uint32_t *image;
	uint32_t part;
	uint32_t offset;
	uint32_t length;
	uint32_t limit;
	uint32_t base;
	uint32_t index;
	int error;

	/* The part, where in it, and how many words; they must be all there and fit. */
	part = words[2];
	offset = words[3];
	length = words[4];
	if (length != count - FIRMWARE_HEAD_WORDS)
		return EINVAL;
	limit = IMAGE_IMEM_WORDS;
	base = 0U;
	if (part == FIRMWARE_PART_DMEM) {
		limit = IMAGE_DMEM_WORDS;
		base = IMAGE_DMEM_OFFSET / 4U;
	} else if (part != FIRMWARE_PART_IMEM) {
		return EINVAL;
	}
	if (offset > limit || length > limit - offset)
		return EINVAL;

	/* The staging memory, taken with the first piece and kept until the detach. */
	mutex_lock(&device->lock);

	error = 0;
	if (device->fw_image == NULL)
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, IMAGE_BYTES, &device->fw_image);

	/* The words, and how far the part now reaches. */
	if (error == 0) {
		image = (volatile uint32_t *)device->fw_image->view;
		for (index = 0U; index < length; index++)
			image[base + offset + index] = words[FIRMWARE_HEAD_WORDS + index];
		if (part == FIRMWARE_PART_IMEM && offset + length > device->fw_iwords)
			device->fw_iwords = offset + length;
		if (part == FIRMWARE_PART_DMEM && offset + length > device->fw_dwords)
			device->fw_dwords = offset + length;
	}

	mutex_unlock(&device->lock);

	/* No memory for the image. */
	if (error != 0)
		return error;

	/* Succeeded: the words are staged. */
	return 0;
}

/*
 * Loads the staged image and starts the firmware again: the caller must be
 * the only session, so no other work is lost.  Through local memory, the
 * image is first copied there with the debug port while the processor
 * still runs.
 */
static int
firmware_reload(
	struct ghci_device *device,
	uint32_t flags)
{
	volatile uint32_t *image;
	uint32_t iwords;
	uint32_t dwords;
	int via_local;
	int alone;
	int error;

	/* Only the caller's session is open, and an image of both parts is staged. */
	mutex_lock(&device->lock);

	alone = 0;
	if (device->session_count == 1U)
		alone = 1;
	image = NULL;
	if (device->fw_image != NULL)
		image = (volatile uint32_t *)device->fw_image->view;
	iwords = device->fw_iwords;
	dwords = device->fw_dwords;
	via_local = 0;
	if ((flags & FIRMWARE_FLAG_VIA_LOCAL) != 0U)
		via_local = 1;

	/* The local copy's memory, taken the first time and kept until the detach. */
	error = 0;
	if (alone && via_local && device->fw_image_local == NULL)
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DEVICE, IMAGE_BYTES, &device->fw_image_local);

	mutex_unlock(&device->lock);

	/* Refuses a reload that would take others' work away, one with half an image, or one with no local memory. */
	if (!alone) {
		kern_logf("ghci: %s: firmware reload: other sessions are open\n", device->name);
		return EBUSY;
	}
	if (image == NULL || iwords == 0U || dwords == 0U)
		return EINVAL;
	if (error != 0) {
		kern_logf("ghci: %s: firmware reload: no local memory for the image (%d)\n", device->name, error);
		return error;
	}

	/* The image reaches the GPU before the load reads it: from host memory, or from its copy in local memory. */
	view_clean(device, image, IMAGE_BYTES);
	kern_io_write_barrier();
	device->fw_ibase = device->fw_image->gpu_address;
	device->fw_dbase = device->fw_ibase + IMAGE_DMEM_OFFSET;
	if (via_local) {
		error = image_to_local(device, device->fw_image_local->gpu_address);
		if (error != 0)
			return EIO;
	}

	/* The reset, the load, the start and the handshake. */
	kern_logf("ghci: %s: firmware reload: %u IMEM words, %u DMEM words, from %08x\n",
	    device->name,
	    (unsigned)iwords,
	    (unsigned)dwords,
	    (unsigned)device->fw_ibase);
	error = ghci_cp_reload(device);

	/* The staged words are used up either way: the next image starts from nothing. */
	mutex_lock(&device->lock);

	device->fw_iwords = 0U;
	device->fw_dwords = 0U;

	mutex_unlock(&device->lock);

	/* Reports a load or a start that failed (the log says which). */
	if (error != 0)
		return error;

	/* Succeeded: the new firmware runs and answered its INIT. */
	return 0;
}

/*
 * Copies the staged image into local memory through the debug port, IMEM
 * at `local` and DMEM IMAGE_DMEM_OFFSET after it, checks two words of it,
 * and makes them the load's addresses.
 */
static int
image_to_local(
	struct ghci_device *device,
	uint32_t local)
{
	volatile uint32_t *image;
	uint32_t index;
	uint32_t read;
	int error;

	/* IMEM's words, one write of the debug port each. */
	kern_logf("ghci: %s: firmware reload: copying the image to local memory at %08x\n", device->name, (unsigned)local);
	image = (volatile uint32_t *)device->fw_image->view;
	for (index = 0U; index < device->fw_iwords; index++) {
		error = dbg_write(device, local + index * 4U, image[index]);
		if (error != 0) {
			kern_logf("ghci: %s: firmware reload: debug write of IMEM word %u failed (%d)\n", device->name, (unsigned)index, error);
			return error;
		}
	}

	/* DMEM's words. */
	for (index = 0U; index < device->fw_dwords; index++) {
		error = dbg_write(device, local + IMAGE_DMEM_OFFSET + index * 4U, image[IMAGE_DMEM_OFFSET / 4U + index]);
		if (error != 0) {
			kern_logf("ghci: %s: firmware reload: debug write of DMEM word %u failed (%d)\n", device->name, (unsigned)index, error);
			return error;
		}
	}

	/* The last IMEM word read back, as a check of the copy. */
	index = device->fw_iwords - 1U;
	read = 0U;
	error = dbg_read(device, local + index * 4U, &read);
	if (error == 0 && read != image[index])
		error = EIO;
	if (error != 0) {
		kern_logf("ghci: %s: firmware reload: local IMEM word %u reads %08x (%d), not %08x\n",
		    device->name,
		    (unsigned)index,
		    (unsigned)read,
		    error,
		    (unsigned)image[index]);
		return error;
	}

	/* Succeeded: the load reads the copy. */
	device->fw_ibase = local;
	device->fw_dbase = local + IMAGE_DMEM_OFFSET;
	return 0;
}

/* Fills a rectangle of a frame with a colour through the blitter and waits for it. */
static int
paint_fill(
	struct ghci_device *device,
	uint32_t base,
	uint32_t stride,
	uint32_t x,
	uint32_t y,
	uint32_t width,
	uint32_t height,
	uint32_t color)
{
	struct ghci_blit blit;
	int error;

	/* A FILL of the rectangle. */
	kern_memset(&blit, 0, sizeof(blit));
	blit.op = GHCI_BLT_FILL;
	blit.dst_base = base;
	blit.dst_stride = stride;
	blit.dst_x = x;
	blit.dst_y = y;
	blit.width = width;
	blit.height = height;
	blit.color = color;
	error = ghci_blit_run(device, &blit);
	if (error != 0) {
		kern_logf("ghci: %s: paint: the fill at (%u, %u) %ux%u failed (%d)\n", device->name, (unsigned)x, (unsigned)y, (unsigned)width, (unsigned)height, error);
		return error;
	}

	/* Succeeded: the rectangle is filled. */
	return 0;
}

/* Gives the colour of a pixel of the picture (the padding of the rows is black). */
static uint32_t
paint_color(
	uint32_t x,
	uint32_t y,
	uint32_t width,
	uint32_t height)
{
	uint32_t band;
	uint32_t step;
	uint32_t grey;

	/* Past the row's pixels: the padding. */
	if (x >= width)
		return 0U;

	/* The bands over the top two thirds. */
	if (y < height * 2U / 3U) {
		band = x * PAINT_BANDS / width;
		while (band + 1U < PAINT_BANDS && x >= (band + 1U) * width / PAINT_BANDS)
			band++;
		while (band > 0U && x < band * width / PAINT_BANDS)
			band--;
		return paint_bands[band];
	}

	/* The grey steps below. */
	step = x * PAINT_STEPS / width;
	while (step + 1U < PAINT_STEPS && x >= (step + 1U) * width / PAINT_STEPS)
		step++;
	while (step > 0U && x < step * width / PAINT_STEPS)
		step--;
	grey = step * 255U / (PAINT_STEPS - 1U);
	return grey << 16 | grey << 8 | grey;
}

/*
 * Has the firmware sum the frame, a megabyte at a time, and compares the
 * sums with the picture's: 0 when they match, EIO when not.
 */
static int
paint_check(
	struct ghci_device *device,
	uint32_t base,
	uint32_t stride,
	uint32_t width,
	uint32_t height)
{
	uint32_t arguments[3];
	uint32_t reply[GHCI_REPLY_WORDS];
	uint32_t bytes;
	uint32_t done;
	uint32_t part;
	uint32_t offset_words;
	uint32_t plain;
	uint32_t weighted;
	uint32_t want_plain;
	uint32_t want_weighted;
	uint32_t index;
	uint32_t word;
	uint32_t x;
	uint32_t y;
	int error;

	/* A firmware without the diagnostics cannot read local memory back: the picture is only to be seen. */
	if ((device->fw_features & GHCI_INIT_F_DIAG) == 0U) {
		kern_logf("ghci: %s: paint: no DIAG_SUM in this firmware; not read back\n", device->name);
		return 0;
	}

	/* The firmware's sums of the whole frame, combined over the parts (the weights go on from part to part). */
	bytes = stride * height;
	plain = 0U;
	weighted = 0U;
	done = 0U;
	while (done < bytes) {
		part = bytes - done;
		if (part > GHCI_DIAG_BYTES_MAX)
			part = GHCI_DIAG_BYTES_MAX;
		arguments[0] = base + done;
		arguments[1] = 0U;
		arguments[2] = part;
		error = packet_run(device, DIAG_REPLY_OFFSET, GHCI_GPU_OP_DIAG_SUM, arguments, 3U, reply, DIAG_WAIT_MS);
		if (error != 0)
			return error;
		if (reply[0] != 0U || reply[1] != part) {
			kern_logf("ghci: %s: paint: DIAG_SUM at %08x: result %08x, %08x bytes\n", device->name, (unsigned)(base + done), (unsigned)reply[0], (unsigned)reply[1]);
			return EIO;
		}
		offset_words = done / 4U;
		plain += reply[GHCI_DIAG_SUM_PLAIN];
		weighted += reply[GHCI_DIAG_SUM_WEIGHTED] + offset_words * reply[GHCI_DIAG_SUM_PLAIN];
		done += part;
	}

	/* The picture's sums, word by word as the frame holds it. */
	want_plain = 0U;
	want_weighted = 0U;
	index = 0U;
	for (y = 0U; y < height; y++) {
		for (x = 0U; x < stride / 4U; x++) {
			word = paint_color(x, y, width, height);
			index++;
			want_plain += word;
			want_weighted += index * word;
		}
	}

	/* The firmware's against the picture's. */
	kern_logf("ghci: %s: paint: the frame read back: sum %08x weighted %08x, the picture's %08x %08x: %s\n",
	    device->name,
	    (unsigned)plain,
	    (unsigned)weighted,
	    (unsigned)want_plain,
	    (unsigned)want_weighted,
	    (plain == want_plain && weighted == want_weighted) ? "PASS" : "FAIL");
	if (plain != want_plain || weighted != want_weighted)
		return EIO;

	/* Succeeded: the frame holds the picture. */
	return 0;
}
