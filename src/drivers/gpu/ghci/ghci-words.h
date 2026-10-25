/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the GHCI driver that are arithmetic on words: the fence
 * comparison, the ring's room and padding, the walk of the extended
 * capability list, the allocator of GPU addresses, the packets the
 * driver writes, the check of a native command stream, the capability
 * record libvulkan reads and the table of job markers.
 *
 * Nothing declared here touches the kernel or the hardware, and no
 * function takes a lock: the caller serializes every call on one object.
 * A host test runs these functions exactly as the kernel does.
 */

#ifndef DRIVERS_GPU_GHCI_WORDS_H
#define DRIVERS_GPU_GHCI_WORDS_H

#include <stddef.h>
#include <stdint.h>

/* The most entries of the extended capability list that are walked. */
#define GHCI_EXTCAP_MAX			32U

/* The most class 0 packets one native command carries. */
#define GHCI_NATIVE_PACKETS_MAX		1024U

/* The words of a native command's head: the gpu-op number, the flags and the packet count. */
#define GHCI_NATIVE_HEAD_DWORDS		3U

/* The dwords of a fence packet: the header, the address and the value. */
#define GHCI_FENCE_PACKET_DWORDS	3U

/* The dwords of a one-word packet without payload (BLIT, WAIT_IDLE, INVALIDATE). */
#define GHCI_BARE_PACKET_DWORDS		1U

/* The dwords of a FLIP packet: the header, the plane, the image, the record and the value. */
#define GHCI_FLIP_PACKET_DWORDS		5U

/* The most (offset, value) pairs of one DISP_SET packet (its payload count is 8 bits). */
#define GHCI_DISP_SET_PAIRS_MAX		127U

/* The state words one blitter operation writes (BLT_OP to BLT_CLIP_MAX). */
#define GHCI_BLIT_STATE_WORDS		11U

/* The dwords of the packets of one blitter operation: a SET_REG of its state and the BLIT. */
#define GHCI_BLIT_PACKET_DWORDS		(1U + 2U * GHCI_BLIT_STATE_WORDS + 1U)

/* The job markers one session holds at once. */
#define GHCI_JOB_MARKERS		64U

/* The bytes of the capability record libvulkan reads (the native renderer's record). */
#define GHCI_CAPSET_BYTES		176U

/*
 * The states of a job marker.
 *
 * A FREE marker belongs to nobody.  RESERVED holds a completion that the
 * core has not published yet.  COMMITTED is bound to a fence value and
 * completes when the fence passes.  FAILED completes with an error at the
 * next retirement, whatever the fence says.
 */
enum ghci_job_state {
	GHCI_JOB_FREE = 0,
	GHCI_JOB_RESERVED,
	GHCI_JOB_COMMITTED,
	GHCI_JOB_FAILED
};

/*
 * One run of GPU addresses in use, kept in address order in the
 * allocator's table.
 */
struct ghci_range_extent {
	uint32_t address;
	uint32_t bytes;
};

/*
 * The allocator of a range [base, limit) of GPU addresses: first fit over
 * a table of the runs in use, sorted by address.
 *
 * The table is the caller's and lives as long as the allocator.  limit is
 * 64 bits wide so that a range may end at 2^32.  count is the number of
 * runs in use; capacity bounds it.
 */
struct ghci_range_map {
	uint64_t base;
	uint64_t limit;
	struct ghci_range_extent *extents;
	unsigned count;
	unsigned capacity;
};

/*
 * What the check of a native command asks of its caller.
 *
 * owned reports 1 when a run of GPU addresses lies wholly inside memory
 * the submitting session owns, and 0 otherwise; context is passed to it
 * unchanged.  tag is the job tag every copied packet carries.
 */
struct ghci_native_check {
	int (*owned)(void *context, uint32_t address, uint32_t bytes);
	void *context;
	uint32_t tag;
};

/*
 * One rectangle operation of the blitter in the words of its state.
 *
 * Coordinates and sizes are pixels; strides are bytes.  op is the whole
 * BLT_OP word (the operation, blend, pixel size and direction bits).
 */
struct ghci_blit {
	uint32_t op;
	uint32_t dst_base;
	uint32_t dst_stride;
	uint32_t src_base;
	uint32_t src_stride;
	uint32_t dst_x;
	uint32_t dst_y;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t width;
	uint32_t height;
	uint32_t color;
};

/*
 * One job marker of a session.
 *
 * completion is the core's opaque record, held from the reservation until
 * the marker is retired or withdrawn.  fence is meaningful once the
 * marker is COMMITTED: the device fence value whose passing completes it.
 */
struct ghci_job_marker {
	enum ghci_job_state state;
	void *completion;
	uint32_t fence;
};

/*
 * The job markers of one session.
 *
 * reserved counts the RESERVED markers and committed the COMMITTED and
 * FAILED ones, so the room left is GHCI_JOB_MARKERS less both.  The
 * session's lock protects the whole table.
 */
struct ghci_job_table {
	struct ghci_job_marker markers[GHCI_JOB_MARKERS];
	unsigned reserved;
	unsigned committed;
};

/*
 * Reports whether a fence value in memory has reached a value, with the
 * 32-bit wrap: 1 when it has.
 */
int ghci_fence_passed(uint32_t memory, uint32_t value);

/*
 * Plans where the next need dwords go in the ring: pad receives how many
 * dwords of padding come first (0, or the rest of the ring up to its end).
 * Returns 0, EAGAIN while the ring has no room yet, or EINVAL for a request
 * no ring of this size can take.
 */
int ghci_ring_plan(uint32_t wptr, uint32_t rptr, uint32_t ring_dwords, uint32_t need, uint32_t *pad);

/*
 * Walks the extended capability list from its first entry for the entry
 * of an id, reading the window's words through read.  Returns 0 with the
 * entry's register offset and version, ENOENT without such an entry, or
 * EINVAL for a list that leaves the window or does not end.
 */
int ghci_extcap_find(uint32_t (*read)(void *context, uint32_t offset), void *context, uint32_t first, uint32_t window_bytes, uint32_t id, uint32_t *registers, uint32_t *version);

/* Starts an allocator over [base, limit) with the caller's table. */
void ghci_range_init(struct ghci_range_map *map, uint64_t base, uint64_t limit, struct ghci_range_extent *extents, unsigned capacity);

/*
 * Takes bytes of addresses aligned to alignment (a power of two), the
 * first run that fits.  Returns 0 with the address, ENOMEM when nothing
 * fits, ENOSPC when the table is full, or EINVAL for a bad request.
 */
int ghci_range_alloc(struct ghci_range_map *map, uint32_t bytes, uint32_t alignment, uint32_t *address);

/* Gives back the run that starts at address.  Returns 0 or ENOENT. */
int ghci_range_free(struct ghci_range_map *map, uint32_t address);

/* Reports the bytes of the range that are still free. */
uint64_t ghci_range_free_bytes(const struct ghci_range_map *map);

/* Reports whether [address, address + bytes) lies wholly inside one run in use: 1 when it does. */
int ghci_range_holds(const struct ghci_range_map *map, uint32_t address, uint32_t bytes);

/* Writes a complete fence packet of value to address; irq nonzero raises the fence interrupt. */
void ghci_packet_fence(uint32_t tag, uint32_t address, uint32_t value, int irq, uint32_t *out);

/* Writes a padding packet of dwords dwords in all (at least 1). */
void ghci_packet_pad(uint32_t dwords, uint32_t *out);

/* Writes a WAIT_IDLE packet for the units of a STATUS busy-bit mask (0: all). */
void ghci_packet_wait_idle(uint32_t tag, uint32_t units, uint32_t *out);

/* Writes an INVALIDATE packet of the caches named by flags. */
void ghci_packet_invalidate(uint32_t tag, uint32_t caches, uint32_t *out);

/*
 * Writes a DISP_SET packet of pair_count (offset, value) pairs of the
 * display controller's registers, given in pairs as offset, value,
 * offset, value; wait_idle makes the processor wait afterwards until the
 * controller is idle.  Returns 0 with the dwords written (1 + 2 x
 * pair_count), or EINVAL for no pair or more than one packet holds.
 */
int ghci_packet_disp_set(uint32_t tag, int wait_idle, const uint32_t *pairs, uint32_t pair_count, uint32_t *out, uint32_t *dwords);

/*
 * Writes a FLIP packet (GHCI_FLIP_PACKET_DWORDS dwords) that makes image
 * the plane's pending image and writes {value, FRAME_COUNT} to record at
 * its take; complete makes it wait first for every earlier command.
 */
void ghci_packet_flip(uint32_t tag, int complete, uint32_t plane, uint32_t image, uint32_t record, uint32_t value, uint32_t *out);

/*
 * Writes the packets of one blitter operation: a SET_REG of its state
 * (no clipping) and a BLIT.  Returns 0 with the dwords written, or ENOSPC
 * when they do not fit in capacity.
 */
int ghci_packet_blit(const struct ghci_blit *blit, uint32_t tag, uint32_t *out, uint32_t capacity, uint32_t *dwords);

/*
 * Checks a GPU_OP_GHCI_NATIVE command and copies its packets to out with
 * the caller's job tag.  Returns 0 with the dwords written, E2BIG when
 * they do not fit in capacity, or EINVAL for anything the check refuses.
 */
int ghci_native_prepare(const uint32_t *stream, uint32_t stream_dwords, const struct ghci_native_check *check, uint32_t *out, uint32_t capacity, uint32_t *dwords);

/*
 * Fills the capability record of the native renderer protocol that
 * libvulkan reads into data (GHCI_CAPSET_BYTES bytes).
 */
void ghci_capset_fill(uint8_t *data);

/* Empties a session's table of job markers. */
void ghci_jobs_init(struct ghci_job_table *table);

/* Takes a free marker for completion.  Returns 0 with its index, or EAGAIN when every marker is in use. */
int ghci_jobs_reserve(struct ghci_job_table *table, void *completion, unsigned *index);

/*
 * Binds a reserved marker to a fence value.  passed says whether the
 * fence has already passed (completed is the last completed value): the
 * marker is then freed at once and the caller completes it.  Returns 0,
 * or ESTALE when the marker does not hold completion in the reserved state.
 */
int ghci_jobs_commit(struct ghci_job_table *table, unsigned index, void *completion, uint32_t fence, uint32_t completed, int *passed);

/*
 * Withdraws a reserved marker (fault 0: freed, nothing completes) or ends
 * it with an error (fault 1: FAILED, completed at the next retirement).
 * Returns 0, ESTALE for a marker that does not hold completion as a
 * cancel allows, or EINVAL for another fault value.
 */
int ghci_jobs_cancel(struct ghci_job_table *table, unsigned index, void *completion, unsigned fault);

/* Reports how many markers a reservation could take now. */
unsigned ghci_jobs_capacity(const struct ghci_job_table *table);

/* Reports the index of the marker a token points at.  Returns 0, or ESTALE for another pointer. */
int ghci_jobs_index(const struct ghci_job_table *table, const void *token, unsigned *index);

/*
 * Frees every marker that is done: a COMMITTED one whose fence has passed
 * completed, and a FAILED one.  Each freed marker's completion goes to
 * done[] and its error (0 or EIO) to errors[], at most capacity of them.
 * Returns the number written.
 */
unsigned ghci_jobs_retire(struct ghci_job_table *table, uint32_t completed, void **done, int *errors, unsigned capacity);

/* Turns every COMMITTED marker into FAILED, for a device that lost its work. */
void ghci_jobs_fail(struct ghci_job_table *table);

#endif
