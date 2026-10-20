/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A check, in a test kernel, of the core's compute path: the driver's
 * own programs run on the core without a user program, and what they
 * wrote is read back.
 *
 * It reports three lines.  The first says whether a compute kernel wrote
 * every word of a 64 KiB window, read through the uncached alias.  The
 * second names the path of the core's data masters, the stores of the
 * shading cluster, and checks whether the processor's cached view sees
 * them: clean lines loaded before the core writes, eight rounds of
 * 64 KiB across 2 MiB, nothing done about the core's system level cache,
 * the verdict from the cached view only.  The third tells whether the
 * task issue counts temporaries in blocks of four or of two, where the
 * hardware shows it.  None of them changes how memory is shared.
 *
 * Procedure referenced:
 * plan/ws256/design-peripherals.md section 2.4 (controls 0b, 1 and 6:
 * a clean line loaded before the device writes, eight rounds of 64 KiB
 * across 2 MiB, the verdict taken from the cached view only, and the
 * range dropped from the caches afterwards).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/dcache.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/render-sr-program.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/compute-program.h"

/*
 * The run the rounds use, the rounds, how far apart they are, the bytes
 * each covers, and the steps one instance takes over a window.
 */
#define CHECK_BYTES		0x200000U
#define CHECK_ROUNDS		8U
#define CHECK_STRIDE		0x20000U
#define CHECK_WINDOW		0x10000U
#define CHECK_WORDS		(CHECK_WINDOW / 4U)
#define CHECK_STEPS		(CHECK_WINDOW / POWERVR_COMPUTE_STORE_STRIDE)

/* Where in the run the temporaries programs write, past every window. */
#define TEMPS_OFFSET		0x1f0000U

/*
 * The device addresses of the run, of the buffer holding the control
 * streams and the resume area, and of the data sequencer and shading
 * cluster programs, each in its heap with room before it.
 */

/* The stream buffer: the resume area first, then one stream per kick. */
#define STREAM_BYTES		0x10000U
#define STREAM_FIRST		0x1000U
#define STREAM_SPACING		0x100U

/* The data sequencer buffer: one 64-byte slot per program, data then code. */
#define PDS_BYTES		0x1000U
#define PDS_SLOT		0x40U
#define PDS_CODE		0x10U

/* The shading cluster buffer: one 32 KiB room per program. */
#define USC_ROOM		0x8000U
#define USC_PROGRAMS		(CHECK_ROUNDS + 1U)
#define USC_BYTES		(USC_ROOM * USC_PROGRAMS)

/*
 * The kicks: one per round, then the temporaries program issued with
 * blocks of four and of two; the fence program takes the slot after.
 */
#define KICK_TEMPS_FOUR		CHECK_ROUNDS
#define KICK_TEMPS_TWO		(CHECK_ROUNDS + 1U)
#define KICK_COUNT		(CHECK_ROUNDS + 2U)

/* The probe kicks' streams, after the fence program's slot. */
#define KICK_END_ONLY		(KICK_COUNT + 1U)
#define KICK_FENCE_ONLY		(KICK_COUNT + 2U)
#define PDS_FENCE		KICK_COUNT

/* The data sequencer temporaries a compute kernel's inputs take. */
#define PDS_INPUT_TEMPORARIES	3U

/* The task size of the fence kernel. */
#define FENCE_INSTANCES		8U

/* The bits of check_state.mapped: which buffers the session maps. */
#define MAPPED_RUN		0x1U
#define MAPPED_STREAMS		0x2U
#define MAPPED_PDS		0x4U
#define MAPPED_USC		0x8U

/* How long one kick may take. */
#define CHECK_WAIT_MS		2000U

/* What the control found: whether the processor alone can be shown a stale line. */
enum control_result {
	CONTROL_OK = 0,
	CONTROL_INCONCLUSIVE
};

/* The buffers of the check, where the core sees them, and the session they are mapped in. */
/*
 * Where the run and the streams are mapped: places the session's general
 * heap hands out, so they never meet what the session itself mapped
 * there (the check runs once, on one thread).
 */
static uint64_t run_address;
static uint64_t stream_address;

/* Where the programs are mapped, as offsets in their heaps: places the session's program heaps hand out. */
static uint32_t pds_offset;
static uint32_t usc_offset;

struct check_state {
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_buffer *run;
	struct powervr_buffer *streams;
	struct powervr_buffer *pds;
	struct powervr_buffer *usc;
	volatile uint32_t *alias;
	const volatile uint32_t *cached;
	unsigned mapped;
	unsigned placed;
};

/* What the rounds found, word by word. */
struct check_counts {
	unsigned rounds_matched;
	uint64_t stale;
	uint64_t unwritten;
	int error;
};

static int setup(struct check_state *state, const char **step);
static void teardown(struct check_state *state);
static int take_buffer(struct check_state *state, uint64_t bytes, uint64_t address, struct powervr_buffer **result);
static int take_program(struct check_state *state, uint64_t bytes, enum powervr_program_heap heap, struct powervr_buffer **result, uint32_t *offset);
static int write_programs(struct check_state *state);
static int write_kick(struct check_state *state, unsigned kick, unsigned usc_program, unsigned temporaries, unsigned field);
static int write_probe_streams(struct check_state *state);
static int probe_kick(struct check_state *state, unsigned kick_index, const char *what);
static void put_bytes(struct powervr_buffer *buffer, uint32_t offset, const uint8_t *bytes, size_t length);
static void put_word(struct powervr_buffer *buffer, uint32_t offset, uint32_t word);
static int kick(struct check_state *state, unsigned kick_index);
static int run_kernel(struct check_state *state, unsigned *written);
static enum control_result run_control(struct check_state *state);
static void run_round(struct check_state *state, unsigned round, struct check_counts *counts);
static void run_coherency(struct check_state *state);
static int run_temps(struct check_state *state, unsigned kick_index, unsigned *wrong);
static void report_temps(int four_error, unsigned four_wrong, int two_error, unsigned two_wrong);
static uint32_t pattern(unsigned round, uint32_t word);
static uint32_t before(unsigned round, uint32_t word);
static void load_cached(const volatile uint32_t *words, uint32_t count);

/*
 * Runs the driver's own compute work on the core and reports what it saw.
 */
void
powervr_compute_check(
	struct powervr_render *render)
{
	struct check_state state;
	const char *step;
	unsigned written;
	unsigned four_wrong;
	unsigned two_wrong;
	int four_error;
	int two_error;
	int error;

	/* Opens a session of the check's own, with its buffers mapped and its programs written. */
	state.render = render;
	step = "open";
	error = setup(&state, &step);
	if (error != 0) {
		kern_logf("powervr: compute kernel: not run, %s (%d)\n", step, error);
		teardown(&state);
		return;
	}

	/* The kernel alone: every word of the first window, read through the alias. */
	written = 0;
	error = run_kernel(&state, &written);
	if (error != 0) {
		kern_logf("powervr: compute kernel: FAILED (%d), %u/%u words written\n", error, written, (unsigned)CHECK_WORDS);
		powervr_firmware_dump(state.render->core, POWERVR_KERNEL_CCB_SLOTS);
	} else if (written != CHECK_WORDS) {
		kern_logf("powervr: compute kernel: FAILED, %u/%u words written\n", written, (unsigned)CHECK_WORDS);
		powervr_firmware_dump(state.render->core, POWERVR_KERNEL_CCB_SLOTS);
	} else {
		kern_logf("powervr: compute kernel: OK, %u/%u words written\n", written, (unsigned)CHECK_WORDS);
	}

	/* A kick the core never finished may still write; everything stays with the core. */
	if (error == ETIMEDOUT) {
		kern_logf("powervr: compute check: stopped, its memory left to the core\n");
		return;
	}

	/* The coherency rounds, only over a kernel that wrote what it should. */
	if (error == 0 && written == CHECK_WORDS)
		run_coherency(&state);

	/* The temporaries field, read both ways. */
	four_wrong = 0;
	two_wrong = 0;
	four_error = run_temps(&state, KICK_TEMPS_FOUR, &four_wrong);
	two_error = 0;
	if (four_error != ETIMEDOUT)
		two_error = run_temps(&state, KICK_TEMPS_TWO, &two_wrong);
	report_temps(four_error, four_wrong, two_error, two_wrong);
	if (four_error == ETIMEDOUT || two_error == ETIMEDOUT) {
		kern_logf("powervr: compute check: stopped, its memory left to the core\n");
		return;
	}

	/* Unmaps and returns everything. */
	teardown(&state);
}

/* Opens the session, takes and maps the buffers, and writes the programs. */
static int
setup(
	struct check_state *state,
	const char **step)
{
	void *opaque;
	int error;

	/* Nothing is held yet. */
	state->session = NULL;
	state->run = NULL;
	state->streams = NULL;
	state->pds = NULL;
	state->usc = NULL;
	state->mapped = 0;
	state->placed = 0;

	/* A session of the check's own. */
	*step = "open";
	error = state->render->ops.open(state->render, &opaque);
	if (error != 0)
		return error;
	state->session = opaque;

	/* Places for the run and the streams in the session's general heap. */
	*step = "memory places";
	mutex_lock(&state->render->core->lock);

	error = powervr_gpu_space_take(&state->session->space, CHECK_BYTES, &run_address);
	if (error == 0) {
		state->placed |= MAPPED_RUN;
		error = powervr_gpu_space_take(&state->session->space, STREAM_BYTES, &stream_address);
		if (error == 0)
			state->placed |= MAPPED_STREAMS;
	}

	mutex_unlock(&state->render->core->lock);
	if (error != 0)
		return error;

	/* The run the kernels write, with its alias and its cached view. */
	*step = "memory: run";
	error = take_buffer(state, CHECK_BYTES, run_address, &state->run);
	if (error != 0)
		return error;
	state->alias = (volatile uint32_t *)state->run->view;
	state->cached = kern_pmem_to_kernel(state->run->run.paddr);

	/* The control streams and the resume area, the data sequencer and the shading cluster programs. */
	*step = "memory: streams";
	error = take_buffer(state, STREAM_BYTES, stream_address, &state->streams);
	if (error != 0)
		return error;

	*step = "memory: data sequencer programs";
	error = take_program(state, PDS_BYTES, POWERVR_PROGRAM_PDS, &state->pds, &pds_offset);
	if (error != 0)
		return error;
	state->mapped |= MAPPED_PDS;

	*step = "memory: shading cluster programs";
	error = take_program(state, USC_BYTES, POWERVR_PROGRAM_USC, &state->usc, &usc_offset);
	if (error != 0)
		return error;
	state->mapped |= MAPPED_USC;

	/* Has the core drop stale table entries before it walks the new ones. */
	*step = "mmu";
	mutex_lock(&state->render->core->lock);

	error = powervr_command_mmu_flush(state->render->core);

	mutex_unlock(&state->render->core->lock);
	if (error != 0)
		return error;

	/* The programs and the streams. */
	*step = "programs";
	error = write_programs(state);
	if (error != 0)
		return error;

	/* Succeeded: the check can kick. */
	return 0;
}

/* Unmaps the buffers, closes the session and returns the buffers. */
static void
teardown(
	struct check_state *state)
{
	struct powervr_render *render;

	/* Removes the programs' mappings; each unmap takes the lock and flushes. */
	render = state->render;
	if ((state->mapped & MAPPED_PDS) != 0)
		powervr_session_program_unmap(state->session, POWERVR_PROGRAM_PDS, state->pds, POWERVR_PDS_HEAP_BASE + pds_offset);
	if ((state->mapped & MAPPED_USC) != 0)
		powervr_session_program_unmap(state->session, POWERVR_PROGRAM_USC, state->usc, POWERVR_USC_HEAP_BASE + usc_offset);

	/* Removes the mappings the check made. */
	mutex_lock(&render->core->lock);

	if ((state->mapped & MAPPED_RUN) != 0)
		powervr_gpu_mmu_unmap(&state->session->mmu, run_address, state->run->run.size);
	if ((state->mapped & MAPPED_STREAMS) != 0)
		powervr_gpu_mmu_unmap(&state->session->mmu, stream_address, state->streams->run.size);
	if (state->mapped != 0)
		(void)powervr_command_mmu_flush(render->core);
	if ((state->placed & MAPPED_RUN) != 0)
		(void)powervr_gpu_space_give(&state->session->space, run_address);
	if ((state->placed & MAPPED_STREAMS) != 0)
		(void)powervr_gpu_space_give(&state->session->space, stream_address);

	mutex_unlock(&render->core->lock);

	/* Drops every line the cached reads loaded, so nothing stale outlives the check. */
	if (state->run != NULL)
		kern_dcache_invalidate_range((const void *)state->cached, CHECK_BYTES);

	/* Closes the session. */
	if (state->session != NULL)
		render->ops.close(render, state->session);

	/* Returns the buffers. */
	if (state->run != NULL)
		powervr_buffer_put(render, state->run);
	if (state->streams != NULL)
		powervr_buffer_put(render, state->streams);
	if (state->pds != NULL)
		powervr_buffer_put(render, state->pds);
	if (state->usc != NULL)
		powervr_buffer_put(render, state->usc);
}

/*
 * Takes a buffer and maps it at a device address of the session, going
 * around the core's system level cache as every buffer the processor
 * reads or writes does.
 */
static int
take_buffer(
	struct check_state *state,
	uint64_t bytes,
	uint64_t address,
	struct powervr_buffer **result)
{
	struct powervr_buffer *buffer;
	unsigned flags;
	int error;

	/* A zeroed, physically contiguous buffer with its uncached view. */
	error = powervr_buffer_create(state->render, bytes, &buffer);
	if (error != 0)
		return error;

	/* The attributes of a buffer the processor sees. */
	flags = powervr_gpu_buffer_flags(1, 0);

	/* Maps it at its place. */
	mutex_lock(&state->render->core->lock);

	error = powervr_gpu_mmu_map(&state->session->mmu, address, buffer->run.paddr, buffer->run.size, flags);

	mutex_unlock(&state->render->core->lock);

	/* Not mapped: the buffer goes back. */
	if (error != 0) {
		powervr_buffer_put(state->render, buffer);
		return error;
	}

	/* Notes the mapping by the buffer's place, so teardown removes it. */
	if (address == run_address) {
		state->mapped |= MAPPED_RUN;
	} else {
		state->mapped |= MAPPED_STREAMS;
	}

	/* Succeeded: the buffer is mapped. */
	*result = buffer;
	return 0;
}

/*
 * Writes the shading cluster programs, the data sequencer programs and
 * one control stream per kick.
 */
static int
write_programs(
	struct check_state *state)
{
	uint8_t *code;
	size_t length;
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS];
	uint32_t words[POWERVR_COMPUTE_PDS_CODE_MAX];
	unsigned count;
	unsigned field_four;
	unsigned field_two;
	unsigned round;
	unsigned i;
	int error;

	/* A staging area for one program. */
	code = kern_malloc(USC_ROOM);
	if (code == NULL)
		return ENOMEM;

	/* One store program per round, each over its own window. */
	error = 0;
	for (round = 0; round < CHECK_ROUNDS; round++) {
		error = powervr_compute_store_program(run_address + (uint64_t)round * CHECK_STRIDE, CHECK_STEPS, code, USC_ROOM, &length);
		if (error != 0)
			break;
		put_bytes(state->usc, round * USC_ROOM, code, length);
	}

	/* The temporaries program, after the store programs. */
	if (error == 0) {
		error = powervr_compute_temps_program(run_address + TEMPS_OFFSET, code, USC_ROOM, &length);
		if (error == 0)
			put_bytes(state->usc, CHECK_ROUNDS * USC_ROOM, code, length);
	}

	kern_free(code);
	if (error != 0)
		return error;

	/* The fence program, in the slot after the kicks'. */
	error = powervr_compute_fence_program(data, words, &count);
	if (error != 0)
		return error;

	put_word(state->pds, PDS_FENCE * PDS_SLOT, data[0]);
	put_word(state->pds, PDS_FENCE * PDS_SLOT + 4U, data[1]);
	for (i = 0; i < count; i++)
		put_word(state->pds, PDS_FENCE * PDS_SLOT + PDS_CODE + i * 4U, words[i]);

	/* The probe kicks' streams: the end alone; the fence kernel and the end. */
	error = write_probe_streams(state);
	if (error != 0)
		return error;

	/*
	 * One kick per round, issuing the round's store program.  Its field
	 * is counted in twos: if the core counts in fours, that only asks for
	 * more temporaries than the program uses.
	 */
	error = powervr_compute_temporaries_field(POWERVR_COMPUTE_STORE_TEMPORARIES, 2, &field_two);
	if (error != 0)
		return error;

	for (round = 0; round < CHECK_ROUNDS; round++) {
		error = write_kick(state, round, round, POWERVR_COMPUTE_STORE_TEMPORARIES, field_two);
		if (error != 0)
			return error;
	}

	/* The temporaries program issued with the field counted in fours, then in twos. */
	error = powervr_compute_temporaries_field(POWERVR_COMPUTE_TEMPS_TEMPORARIES, 4, &field_four);
	if (error != 0)
		return error;

	error = powervr_compute_temporaries_field(POWERVR_COMPUTE_TEMPS_TEMPORARIES, 2, &field_two);
	if (error != 0)
		return error;

	error = write_kick(state, KICK_TEMPS_FOUR, CHECK_ROUNDS, POWERVR_COMPUTE_TEMPS_TEMPORARIES, field_four);
	if (error != 0)
		return error;

	error = write_kick(state, KICK_TEMPS_TWO, CHECK_ROUNDS, POWERVR_COMPUTE_TEMPS_TEMPORARIES, field_two);
	if (error != 0)
		return error;

	/* Succeeded: everything the kicks read is in memory. */
	return 0;
}

/*
 * Writes one kick's data sequencer program and its control stream: the
 * kernel, the fence kernel, the end.
 */
static int
write_kick(
	struct check_state *state,
	unsigned kick_index,
	unsigned usc_program,
	unsigned temporaries,
	unsigned field)
{
	struct powervr_compute_kernel kernel;
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS];
	uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX];
	uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS];
	uint32_t slot;
	uint32_t stream;
	unsigned count;
	unsigned i;
	int error;

	/* The program issuing the shading cluster program, in the kick's slot. */
	error = powervr_compute_kick_program(usc_offset + (uint64_t)usc_program * USC_ROOM, field, data, code, &count);
	if (error != 0)
		return error;

	slot = kick_index * PDS_SLOT;
	put_word(state->pds, slot, data[0]);
	put_word(state->pds, slot + 4U, data[1]);
	for (i = 0; i < count; i++)
		put_word(state->pds, slot + PDS_CODE + i * 4U, code[i]);

	/* The kernel: one workgroup of a whole task, on any cluster, no shared registers. */
	kern_memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = pds_offset + slot;
	kernel.code_offset = pds_offset + slot + PDS_CODE;
	kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
	kernel.pds_temporaries = PDS_INPUT_TEMPORARIES;
	kernel.usc_temporaries = temporaries;
	kernel.instances = POWERVR_COMPUTE_INSTANCES;
	kernel.max_instances = 0;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	if (error != 0)
		return error;

	stream = STREAM_FIRST + kick_index * STREAM_SPACING;
	for (i = 0; i < POWERVR_COMPUTE_KERNEL_WORDS; i++)
		put_word(state->streams, stream + i * 4U, words[i]);

	/* The fence kernel: it waits for the work and frees the shared registers. */
	kern_memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = pds_offset + PDS_FENCE * PDS_SLOT;
	kernel.code_offset = pds_offset + PDS_FENCE * PDS_SLOT + PDS_CODE;
	kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
	kernel.instances = 1;
	kernel.max_instances = FENCE_INSTANCES;
	kernel.fence = 1;
	kernel.shared_from_pds = 1;
	kernel.common_shared = 1;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	if (error != 0)
		return error;

	stream += POWERVR_COMPUTE_KERNEL_WORDS * 4U;
	for (i = 0; i < POWERVR_COMPUTE_KERNEL_WORDS; i++)
		put_word(state->streams, stream + i * 4U, words[i]);

	/* The end of the stream. */
	stream += POWERVR_COMPUTE_KERNEL_WORDS * 4U;
	put_word(state->streams, stream, powervr_compute_terminate_word());

	/* Succeeded: the kick's program and stream are written. */
	return 0;
}

/* Copies bytes into a buffer through its uncached view. */
static void
put_bytes(
	struct powervr_buffer *buffer,
	uint32_t offset,
	const uint8_t *bytes,
	size_t length)
{
	size_t i;

	/* One byte at a time; the view is uncached, so each reaches memory. */
	for (i = 0; i < length; i++)
		buffer->view[offset + i] = bytes[i];
}

/* Writes one little-endian word into a buffer through its uncached view. */
static void
put_word(
	struct powervr_buffer *buffer,
	uint32_t offset,
	uint32_t word)
{
	/* One aligned store through the view. */
	*(volatile uint32_t *)(buffer->view + offset) = word;
}

/* Sends one kick's compute command and waits, bounded, for it to finish. */
static int
kick(
	struct check_state *state,
	unsigned kick_index)
{
	uint32_t command[POWERVR_COMPUTE_COMMAND_WORDS];
	uint32_t value;
	int error;

	/* The command names the kick's stream and the shared resume area. */
	powervr_compute_command(stream_address + STREAM_FIRST + (uint64_t)kick_index * STREAM_SPACING, stream_address, command);
	if (POWERVR_COMPUTE_SWITCH_STATE)
		command[POWERVR_COMPUTE_RESUME_PDS1_WORD] = powervr_compute_resume_pds1(0);

	/* Sends it with the next timeline value. */
	mutex_lock(&state->render->core->lock);

	state->session->submitted++;
	value = state->session->submitted;
	error = powervr_session_kick_compute(state->render, state->session, value, command, POWERVR_COMPUTE_COMMAND_WORDS);

	mutex_unlock(&state->render->core->lock);
	if (error != 0)
		return error;

	/* Waits for the timeline to reach it. */
	error = powervr_session_wait(state->session, value, CHECK_WAIT_MS);
	if (error != 0)
		return error;

	/* Succeeded: the kick finished. */
	return 0;
}

/*
 * Runs the first round's kernel over a window filled with other values
 * and counts, through the alias, the words it wrote.
 */
static int
run_kernel(
	struct check_state *state,
	unsigned *written)
{
	uint32_t word;
	int error;

	/*
	 * Two smaller kicks first, to tell which part locks the compute data
	 * master up: a stream of its end alone, then one of the fence kernel
	 * and the end.  A kick that fails stops the check there.
	 */
	error = probe_kick(state, KICK_END_ONLY, "end only");
	if (error != 0)
		return error;
	error = probe_kick(state, KICK_FENCE_ONLY, "fence and end");
	if (error != 0)
		return error;

	/* Fills the window with values the kernel never writes. */
	for (word = 0; word < CHECK_WORDS; word++)
		state->alias[word] = before(0, word);

	/* Runs the kernel. */
	error = kick(state, 0);
	if (error != 0)
		return error;

	/* Counts the words that hold their own address. */
	*written = 0;
	for (word = 0; word < CHECK_WORDS; word++) {
		if (state->alias[word] == pattern(0, word))
			(*written)++;
	}

	/* Succeeded: the count is the caller's to judge. */
	return 0;
}

/*
 * The control: loads clean lines through the cached view, writes other
 * values through the alias, and reads the cached view again.  Seeing the
 * old values shows the processor alone can be shown a stale line, so a
 * matching round later means something.
 */
static enum control_result
run_control(
	struct check_state *state)
{
	uint32_t word;
	uint32_t stale;

	/* Fills the first window through the alias and drops any line of it. */
	for (word = 0; word < CHECK_WORDS; word++)
		state->alias[word] = before(0, word);
	kern_dcache_invalidate_range((const void *)state->cached, CHECK_WINDOW);

	/* Loads the window into the cache, clean. */
	load_cached(state->cached, CHECK_WORDS);

	/* Writes other values through the alias, then counts the old ones the cached view still shows. */
	for (word = 0; word < CHECK_WORDS; word++)
		state->alias[word] = pattern(0, word);
	stale = 0;
	for (word = 0; word < CHECK_WORDS; word++) {
		if (state->cached[word] == before(0, word))
			stale++;
	}

	/* Drops the lines again, so the rounds start clean. */
	kern_dcache_invalidate_range((const void *)state->cached, CHECK_WINDOW);

	/* Every word stale: the control holds. */
	if (stale == CHECK_WORDS)
		return CONTROL_OK;

	/* The control did not hold. */
	return CONTROL_INCONCLUSIVE;
}

/*
 * One round: loads a window's clean lines, has a kernel write every word
 * of it, and counts what the cached view and the alias show.
 */
static void
run_round(
	struct check_state *state,
	unsigned round,
	struct check_counts *counts)
{
	const volatile uint32_t *cached;
	volatile uint32_t *alias;
	uint32_t word;
	uint32_t matched;
	uint32_t value;
	int error;

	/* The window of this round. */
	alias = state->alias + round * (CHECK_STRIDE / 4U);
	cached = state->cached + round * (CHECK_STRIDE / 4U);

	/* Fills it through the alias with the values from before, and drops any line of it. */
	for (word = 0; word < CHECK_WORDS; word++)
		alias[word] = before(round, word);
	kern_dcache_invalidate_range((const void *)cached, CHECK_WINDOW);

	/* Loads its lines into the cache, clean. */
	load_cached(cached, CHECK_WORDS);

	/* Has the kernel write every word; no cache of the core is flushed. */
	error = kick(state, round);
	if (error != 0) {
		counts->error = error;
		return;
	}

	/* Counts the words the cached view shows written, and those it shows stale. */
	matched = 0;
	for (word = 0; word < CHECK_WORDS; word++) {
		value = cached[word];
		if (value == pattern(round, word)) {
			matched++;
		} else if (value == before(round, word)) {
			counts->stale++;
		}
	}

	/* Counts the words that never reached memory, through the alias. */
	for (word = 0; word < CHECK_WORDS; word++) {
		if (alias[word] != pattern(round, word))
			counts->unwritten++;
	}

	/* The round matched when the cached view showed every word written. */
	if (matched == CHECK_WORDS)
		counts->rounds_matched++;
}

/*
 * Runs the control and the rounds and reports what the cached view saw
 * of the shading cluster's stores, on one line.
 */
static void
run_coherency(
	struct check_state *state)
{
	struct check_counts counts;
	enum control_result control;
	const char *verdict;
	const char *control_word;
	unsigned round;

	/* The control: the processor alone can be shown a stale line. */
	control = run_control(state);

	/* Runs the rounds, each 128 KiB further into the run. */
	counts.rounds_matched = 0;
	counts.stale = 0;
	counts.unwritten = 0;
	counts.error = 0;
	for (round = 0; round < CHECK_ROUNDS; round++) {
		run_round(state, round, &counts);
		if (counts.error != 0)
			break;
	}

	/* A round the core did not finish says nothing. */
	if (counts.error != 0) {
		kern_logf("powervr: coherency (data master writes): not finished (%d)\n", counts.error);
		return;
	}

	/* Names the outcome: writes that never reached memory say nothing about the caches. */
	if (counts.unwritten != 0U) {
		verdict = "INCONCLUSIVE, writes missing";
	} else if (counts.rounds_matched == CHECK_ROUNDS && control == CONTROL_OK) {
		verdict = "COHERENT";
	} else if (counts.rounds_matched == CHECK_ROUNDS) {
		verdict = "MATCHED WITHOUT CONTROL";
	} else {
		verdict = "NOT COHERENT";
	}

	/* The control's word. */
	if (control == CONTROL_OK) {
		control_word = "OK";
	} else {
		control_word = "INCONCLUSIVE";
	}

	/* Reports the outcome on one line; the sharing stays as it is whatever it says. */
	kern_logf("powervr: coherency (data master writes, slc bypass): %s, control %s, %u/%u rounds, %llu stale, %llu missing\n",
		  verdict,
		  control_word,
		  counts.rounds_matched,
		  (unsigned)CHECK_ROUNDS,
		  (unsigned long long)counts.stale,
		  (unsigned long long)counts.unwritten);
}

/*
 * Runs the temporaries program through one of its kicks and counts the
 * instances whose word is not their number.
 */
static int
run_temps(
	struct check_state *state,
	unsigned kick_index,
	unsigned *wrong)
{
	volatile uint32_t *words;
	uint32_t instance;
	int error;

	/* Fills the instances' words with a value no instance writes. */
	words = state->alias + TEMPS_OFFSET / 4U;
	for (instance = 0; instance < POWERVR_COMPUTE_INSTANCES; instance++)
		words[instance] = 0xffffffffU;

	/* Runs the program. */
	error = kick(state, kick_index);
	if (error != 0)
		return error;

	/* Counts the words that are not their instance's number. */
	*wrong = 0;
	for (instance = 0; instance < POWERVR_COMPUTE_INSTANCES; instance++) {
		if (words[instance] != instance)
			(*wrong)++;
	}

	/* Succeeded: the count is the caller's to judge. */
	return 0;
}

/*
 * Reports how the task issue counts temporaries, on one line.
 *
 * Counted in fours, the field asks for exactly the eight temporaries
 * the program uses; counted in twos it asks for sixteen.  If the core
 * counts in twos, the first gives each instance four, and the program
 * sees a neighbour's fill when the instances' temporaries lie next to
 * each other.  The second must always work; if it does not, the check
 * itself is broken.
 */
static void
report_temps(
	int four_error,
	unsigned four_wrong,
	int two_error,
	unsigned two_wrong)
{
	const char *reading;

	/* Reads the two outcomes. */
	if (four_error != 0 || two_error != 0) {
		reading = "NOT RUN";
	} else if (two_wrong != 0) {
		reading = "BROKEN, the larger field failed";
	} else if (four_wrong != 0) {
		reading = "BLOCKS OF 2";
	} else {
		reading = "INCONCLUSIVE, no clobber seen (blocks of 4 not excluded)";
	}

	/* Reports both counts and the reading. */
	kern_logf("powervr: compute temporaries: fours %u/%u wrong (%d), twos %u/%u wrong (%d): %s\n",
		  four_wrong,
		  (unsigned)POWERVR_COMPUTE_INSTANCES,
		  four_error,
		  two_wrong,
		  (unsigned)POWERVR_COMPUTE_INSTANCES,
		  two_error,
		  reading);
}

/* The value a kernel writes to a word of a round's window: the word's own device address. */
static uint32_t
pattern(
	unsigned round,
	uint32_t word)
{
	uint64_t address;

	/* The low half of the word's device address. */
	address = run_address + (uint64_t)round * CHECK_STRIDE + (uint64_t)word * 4U;

	/* Succeeded: the value. */
	return (uint32_t)address;
}

/* The value a word holds before the kernel writes it. */
static uint32_t
before(
	unsigned round,
	uint32_t word)
{
	/* Every bit of the written value flipped. */
	return ~pattern(round, word);
}

/* Reads every word through the cached view, so its lines are loaded clean. */
static void
load_cached(
	const volatile uint32_t *words,
	uint32_t count)
{
	uint32_t word;
	uint32_t sum;

	/* Touches each word; the sum only keeps the reads. */
	sum = 0;
	for (word = 0; word < count; word++)
		sum += words[word];
	(void)sum;
}

/* Takes a zeroed buffer for programs and maps it where the session's program heap places it; reports its offset in the heap. */
static int
take_program(
	struct check_state *state,
	uint64_t bytes,
	enum powervr_program_heap heap,
	struct powervr_buffer **result,
	uint32_t *offset)
{
	struct powervr_buffer *buffer;
	uint64_t address;
	uint64_t base;
	int error;

	/* The buffer. */
	error = powervr_buffer_create(state->render, bytes, &buffer);
	if (error != 0)
		return error;

	/* Its place in the heap. */
	error = powervr_session_program_map(state->session, heap, buffer, &address);
	if (error != 0) {
		powervr_buffer_put(state->render, buffer);
		return error;
	}

	/* Succeeded: the buffer and its offset from the heap's base. */
	base = heap == POWERVR_PROGRAM_PDS ? POWERVR_PDS_HEAP_BASE : POWERVR_USC_HEAP_BASE;
	*offset = (uint32_t)(address - base);
	*result = buffer;
	return 0;
}

/* Writes the probe kicks' streams: the end alone, and the fence kernel followed by the end. */
static int
write_probe_streams(
	struct check_state *state)
{
	struct powervr_compute_kernel kernel;
	uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS];
	uint32_t stream;
	unsigned i;
	int error;

	/* The end alone. */
	stream = STREAM_FIRST + KICK_END_ONLY * STREAM_SPACING;
	put_word(state->streams, stream, powervr_compute_terminate_word());

	/* The fence kernel as the kicks have it, then the end. */
	kern_memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = pds_offset + PDS_FENCE * PDS_SLOT;
	kernel.code_offset = pds_offset + PDS_FENCE * PDS_SLOT + PDS_CODE;
	kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
	kernel.instances = 1;
	kernel.max_instances = FENCE_INSTANCES;
	kernel.fence = 1;
	kernel.shared_from_pds = 1;
	kernel.common_shared = 1;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	if (error != 0)
		return error;

	stream = STREAM_FIRST + KICK_FENCE_ONLY * STREAM_SPACING;
	for (i = 0; i < POWERVR_COMPUTE_KERNEL_WORDS; i++)
		put_word(state->streams, stream + i * 4U, words[i]);
	put_word(state->streams, stream + POWERVR_COMPUTE_KERNEL_WORDS * 4U, powervr_compute_terminate_word());

	/* Succeeded: both streams are written. */
	return 0;
}

/* Runs one probe kick and reports whether it finished and whether the firmware reset the context. */
static int
probe_kick(
	struct check_state *state,
	unsigned kick_index,
	const char *what)
{
	int error;

	/* The kick, between its two lines. */
	kern_logf("powervr: kick: compute probe %s sent (context %s)\n", what, state->session->lost ? "reset" : "kept");
	error = kick(state, kick_index);
	kern_logf("powervr: compute probe: %s: %s (%d), context %s\n", what, error == 0 ? "finished" : "FAILED", error,
		  state->session->lost ? "reset" : "kept");
	if (error == 0 && state->session->lost)
		error = EIO;
	if (error != 0)
		powervr_firmware_dump(state->render->core, POWERVR_KERNEL_CCB_SLOTS);
	return error;
}
