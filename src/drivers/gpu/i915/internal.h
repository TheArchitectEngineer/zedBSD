/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Private state shared only by the i915 GPU driver implementation.
 *
 * Register offsets and bits come from the transcribed Linux
 * definitions in linux/i915-regs.inc. Every driver decision is
 * written here and in the sibling sources.
 */

#ifndef DRIVERS_GPU_I915_INTERNAL_H
#define DRIVERS_GPU_I915_INTERNAL_H

#include <drivers/pci.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/waitq.h>
#include <stdint.h>

#include "linux/i915-regs.inc"

/* Every GPU page-table and object granule is one small page. */
#define I915_PAGE_BYTES			4096U

/* The largest contiguous object v1 hands to userspace. */
#define I915_MAX_RESOURCE_BYTES		(16U * 1024U * 1024U)

/* Alder Lake-P names 39 physical address bits from the GPU (dma_mask_size). */
#define I915_DMA_MAX_ADDRESS		((1ULL << 39) - 1ULL)

/* Engine slots owned by this backend; media engines are never initialized. */
#define I915_ENGINE_RCS0		0U
#define I915_ENGINE_BCS0		1U
#define I915_ENGINE_COUNT		2U
#define I915_RCS0_BASE			RENDER_RING_BASE
#define I915_BCS0_BASE			BLT_RING_BASE

/* Forcewake domains held while engine registers are touched. */
#define I915_FORCEWAKE_GT		1U
#define I915_FORCEWAKE_RENDER		2U
#define I915_FORCEWAKE_ALL		(I915_FORCEWAKE_GT | I915_FORCEWAKE_RENDER)
#define I915_FORCEWAKE_DOMAINS		2U

/* Register handshake bounds in milliseconds. */
#define I915_FORCEWAKE_TIMEOUT_MS	50U
#define I915_RESET_TIMEOUT_MS		1000U
#define I915_STOP_TIMEOUT_MS		100U
#define I915_RESET_READY_TIMEOUT_MS	1U

/* Global GTT pages below this index stay unmapped so a null GPU address faults. */
#define I915_GGTT_RESERVED_PAGES	256U

/* Gen11 GT interrupts arrive in two banks of 32 sources each. */
#define I915_IRQ_BANKS			2U

/* Identity registers become valid within about 100 microseconds; this bounds the poll. */
#define I915_IRQ_IDENTITY_POLLS		2000U

/* The private address space: 48-bit canonical, four levels of 512 entries. */
#define I915_PPGTT_LEVELS		4U
#define I915_PPGTT_ENTRIES		I915_PDES
#define I915_PPGTT_ADDRESS_BITS		48U
#define I915_PPGTT_VA_START		0x100000000ULL
#define I915_PPGTT_VA_ALIGN		0x200000ULL
#define I915_PPGTT_ADDRESS_MASK		(((1ULL << I915_PPGTT_ADDRESS_BITS) - 1ULL) & ~((uint64_t)I915_PAGE_BYTES - 1ULL))

/* Execution objects: one status page per engine, one ring per context. */
#define I915_HWSP_BYTES			I915_PAGE_BYTES
#define I915_RING_BYTES			65536U
#define I915_RING_DWORDS		(I915_RING_BYTES / 4U)
#define I915_CSB_ENTRIES		GEN11_CSB_ENTRIES
#define I915_CSB_WRITE_INDEX		ICL_HWS_CSB_WRITE_INDEX
#define I915_REQUEST_SLOTS		32U
#define I915_REQUEST_MAX_DWORDS		64U
#define I915_MOCS_ENTRIES		64U
#define I915_MOCS_UNUSED_INDEX		2U
#define I915_MOCS_UNCACHED_INDEX	3U

/* Context software ids must stay below the idle marker the CSB uses. */
#define I915_CONTEXT_ID_MODULUS		(GEN12_IDLE_CTX_ID - 1U)

/* The descriptor addressing mode for a 48-bit space (Linux intel_lrc.h enum INTEL_LEGACY_64B_CONTEXT). */
#define I915_LEGACY_64B_CONTEXT		3U

/* Request slot states; RESERVED slots hold a framework callback but were never queued. */
#define I915_REQUEST_FREE		0U
#define I915_REQUEST_QUEUED		1U
#define I915_REQUEST_ACTIVE		2U
#define I915_REQUEST_DONE		3U
#define I915_REQUEST_RESERVED		4U
#define I915_REQUEST_RETAINED		5U

/* The native command stream header (plan/ws029/i915-native-stream.md). */
#define I915_STREAM_MAGIC		0x31394958U
#define I915_STREAM_VERSION		1U
#define I915_STREAM_HEADER_BYTES	32U
#define I915_STREAM_RELOCATION_BYTES	16U
#define I915_STREAM_MAX_RELOCATIONS	64U
#define I915_STREAM_MAX_DWORDS		16384U
#define I915_STREAM_ENGINE_RCS0		0U
#define I915_STREAM_ENGINE_BCS0		1U

/* Submission timelines: 1 and 2 name the copy and render engines; 0 is the default copy engine. */
#define I915_TIMELINE_DEFAULT		0U
#define I915_TIMELINE_BCS0		1U
#define I915_TIMELINE_RCS0		2U

/* The engine class of each slot, as the interrupt identity and reset domain name it. */
#define I915_CLASS_RENDER		RENDER_CLASS
#define I915_CLASS_COPY			COPY_ENGINE_CLASS

struct i915_device;
struct i915_session;
struct i915_vk_device;
struct i915_vk_session;
struct i915_engine;
struct drv_gpu_completion;

/*
 * The global GTT: a flat page table in the upper half of BAR0.
 *
 * Every entry outside an allocation points at one zeroed scratch page so a
 * stray GPU access never reaches unrelated memory. The bitmap allocator hands
 * out page-granular ranges to ring buffers, status pages and context images.
 */
struct i915_ggtt {
	unsigned entries;
	unsigned bitmap_words;
	uint32_t *bitmap;
	struct kern_pmem scratch;
	uint64_t scratch_pte;
	unsigned allocated_pages;
};

/*
 * One page-table page of a private address space, kept for release at destroy.
 *
 * The walk itself reads child addresses out of the entries, so this list only
 * has to remember which pages to return to the pool.
 */
struct i915_ppgtt_page {
	struct kern_pmem run;
	struct i915_ppgtt_page *next;
};

/*
 * One session's private 48-bit address space.
 *
 * The scratch chain (page, table, directory, directory pointer) backs every
 * range no object occupies. Ranges are handed out by a bump allocator and
 * never reused within the lifetime of the space.
 */
struct i915_ppgtt {
	struct kern_pmem pml4;
	struct kern_pmem scratch[I915_PPGTT_LEVELS];
	uint64_t scratch_entry[I915_PPGTT_LEVELS];
	struct i915_ppgtt_page *pages;
	unsigned page_count;
	uint64_t next_va;
	unsigned created;

	/* A space whose session closed while quarantined waits on the device list for reset. */
	uint32_t owner;
	struct i915_ppgtt *next;
};

/*
 * One contiguous GPU object with a kernel CPU view.
 *
 * The device list keeps every live object reachable for detach and reset. An
 * object destroyed while its context is quarantined stays on the list until a
 * checked reset proves the GPU no longer names it.
 */
struct i915_gem_object {
	struct kern_pmem run;
	uint64_t bytes;
	unsigned pages;
	void *address;
	uint32_t ggtt_offset;
	unsigned ggtt_pages;
	struct i915_ppgtt *vm;
	uint64_t va;
	uint32_t slot;
	uint64_t handle;
	unsigned quarantined;
	unsigned busy;
	struct i915_gem_object *next;
	struct i915_gem_object *session_next;
};

/*
 * One logical ring context: the image the engine loads plus its own ring.
 *
 * The context image saves the ring head on every switch, so each context owns
 * a ring; the tail the driver writes into the image is the only field it
 * updates between submissions. The descriptor names the image in the GGTT.
 */
struct i915_context {
	struct i915_engine *engine;
	struct i915_ppgtt *vm;
	struct i915_gem_object *image;
	struct i915_gem_object *ring;
	uint32_t *state;
	uint32_t *ring_dwords;
	uint32_t ring_tail;
	uint32_t descriptor_low;
	uint32_t descriptor_high;
	uint32_t sw_id;
	unsigned created;
};

/*
 * One submission: commands emitted into a context ring, retired by seqno.
 *
 * Slots belong to an engine and are recycled once their request retired. A
 * queued request holds its batch until the engine is idle and emits it then,
 * so the ring head saved in the context image is always current when written.
 */
struct i915_request {
	unsigned state;
	uint32_t seqno;
	struct i915_context *context;
	struct i915_session *session;
	struct drv_gpu_completion *completion;
	struct i915_gem_object *batch;
	uint64_t batch_va;
	uint32_t extra[I915_REQUEST_MAX_DWORDS];
	unsigned extra_count;
	unsigned supervised;
	uint64_t submitted_tick;
	int error;
	struct i915_request *next;
};

/*
 * One command streamer: RCS0 or BCS0.
 *
 * The status page carries the seqno breadcrumbs and the context status buffer.
 * The engine executes one context at a time; the queue holds every request
 * that is waiting for the engine to go idle. irq_lock protects the queue,
 * the active request and the counters.
 */
struct i915_engine {
	struct i915_device *device;
	unsigned index;
	unsigned class;
	unsigned instance;
	uint32_t base;
	uint32_t reset_domain;
	uint32_t ccid;
	struct i915_gem_object *hwsp;
	volatile uint32_t *status;
	unsigned csb_head;
	uint32_t next_seqno;
	uint32_t completed_seqno;
	struct i915_request slots[I915_REQUEST_SLOTS];
	struct i915_request *queue_head;
	struct i915_request *queue_tail;
	struct i915_request *active;
	unsigned hw_active;
	unsigned initialized;
	unsigned resetting;
	unsigned reset_count;
	unsigned csb_promotions;
	unsigned csb_completions;
	unsigned csb_errors;
	struct i915_context kernel_context;
	struct i915_ppgtt kernel_vm;
};

/*
 * One attached Intel GPU, owned by PCI from attach to detach.
 *
 * The mutex serializes every controller-level operation from sessions; the
 * IRQ lock protects engine queues and the counters the interrupt handler
 * publishes. The stage name identifies the attach step that stopped a failed
 * start.
 */
struct i915_device {
	struct drv_pci_device *pci;
	const char *stage;
	uint16_t product;
	uint8_t revision;
	struct drv_pci_enable_state enable_state;
	unsigned saved;
	unsigned bus_master;
	unsigned bar0_claimed;
	struct drv_pci_mapping regs;
	struct drv_pci_mapping gtt;
	struct drv_dma_device *dma;
	struct mutex mutex;
	struct spinlock irq_lock;
	struct wait_queue retire_waitq;

	/* Forcewake references per domain; the mutex holder is the only writer. */
	unsigned forcewake_count[I915_FORCEWAKE_DOMAINS];
	unsigned forcewake_held;

	struct i915_ggtt ggtt;

	/* Objects and sessions are numbered for the resource log lines the harness parses. */
	struct i915_gem_object *objects;
	unsigned object_count;
	unsigned quarantined_objects;
	struct i915_ppgtt *quarantined_vms;
	uint32_t next_session;

	struct i915_engine engines[I915_ENGINE_COUNT];
	unsigned mocs_initialized;

	/* Interrupt ownership and the counters the handler publishes under irq_lock. */
	struct drv_pci_irq interrupt;
	unsigned interrupt_count;
	void *interrupt_cookie;
	unsigned irq_enabled;
	uint64_t irq_total;
	uint64_t irq_unknown;
	uint64_t irq_identity_timeouts;
	uint64_t user_interrupts[I915_ENGINE_COUNT];
	uint64_t context_switches[I915_ENGINE_COUNT];
	uint64_t error_interrupts[I915_ENGINE_COUNT];
	uint32_t last_error[I915_ENGINE_COUNT];

	struct drv_gpu_device *gpu;

	/* The native Vulkan executor, attached after execution works. */
	struct i915_vk_device *vk;
	unsigned failed;
	unsigned selftest_passed;
};

/*
 * One open GPU session: a private address space, one context per engine and
 * its numbered objects.
 *
 * The address space is a separate allocation so a quarantined session can
 * hand it to the device at close without allocating on a path that cannot fail.
 */
struct i915_session {
	struct i915_device *device;
	struct i915_vk_session *vk;
	uint32_t identifier;
	struct i915_ppgtt *vm;
	struct i915_context contexts[I915_ENGINE_COUNT];
	struct i915_gem_object *objects;
	struct i915_gem_object *batches;
	unsigned batch_count;
	uint32_t next_slot;
	unsigned resources;
	unsigned quarantined;
	unsigned stopping;
	unsigned pending_requests;
};

/*
 * One decoded native stream: the header fields and where the relocations and
 * batch dwords start inside the caller's copy.
 */
struct i915_stream {
	uint32_t engine;
	uint32_t relocation_count;
	uint32_t batch_dwords;
	const uint8_t *relocations;
	const uint32_t *batch;
};

/* uncore.c: MMIO access, forcewake and reset handshakes. */
uint32_t drv_i915_read32(struct i915_device *device, uint32_t offset);
void drv_i915_write32(struct i915_device *device, uint32_t offset, uint32_t value);
int drv_i915_wait32(struct i915_device *device, uint32_t offset, uint32_t mask, uint32_t value, unsigned timeout_ms);
int drv_i915_forcewake_get(struct i915_device *device, unsigned domains);
int drv_i915_forcewake_put(struct i915_device *device, unsigned domains);
int drv_i915_uncore_init(struct i915_device *device);
int drv_i915_gt_reset(struct i915_device *device);
int drv_i915_domain_reset(struct i915_device *device, uint32_t domains);

/* ggtt.c: global GTT probe, scratch fill, page allocation and PTE updates. */
int drv_i915_ggtt_start(struct i915_device *device);
void drv_i915_ggtt_stop(struct i915_device *device);
int drv_i915_ggtt_alloc(struct i915_device *device, unsigned pages, uint32_t *offset);
void drv_i915_ggtt_free(struct i915_device *device, uint32_t offset, unsigned pages);
int drv_i915_ggtt_insert(struct i915_device *device, uint32_t offset, uint64_t physical, unsigned pages);
void drv_i915_ggtt_clear(struct i915_device *device, uint32_t offset, unsigned pages);

/* ppgtt.c: private four-level address spaces. */
int drv_i915_ppgtt_create(struct i915_ppgtt *vm);
void drv_i915_ppgtt_destroy(struct i915_ppgtt *vm);
int drv_i915_ppgtt_va_alloc(struct i915_ppgtt *vm, uint64_t bytes, uint64_t *va);
int drv_i915_ppgtt_insert(struct i915_ppgtt *vm, uint64_t va, uint64_t physical, unsigned pages);
void drv_i915_ppgtt_clear(struct i915_ppgtt *vm, uint64_t va, unsigned pages);
uint64_t drv_i915_ppgtt_lookup(const struct i915_ppgtt *vm, uint64_t va);

/* gem.c: contiguous objects, their CPU view and GPU bindings. */
int drv_i915_gem_create(struct i915_device *device, uint64_t bytes, struct i915_gem_object **result);
void drv_i915_gem_destroy(struct i915_device *device, struct i915_gem_object *object);
int drv_i915_gem_bind_ggtt(struct i915_device *device, struct i915_gem_object *object);
void drv_i915_gem_unbind_ggtt(struct i915_device *device, struct i915_gem_object *object);
int drv_i915_gem_bind_vm(struct i915_ppgtt *vm, struct i915_gem_object *object);
void drv_i915_gem_unbind_vm(struct i915_gem_object *object);
int drv_i915_gem_read(struct i915_gem_object *object, uint64_t offset, void *buffer, uint32_t bytes);
int drv_i915_gem_write(struct i915_gem_object *object, uint64_t offset, const void *buffer, uint32_t bytes);

/* engine.c: command streamer bring-up, MOCS, reset and interrupt-driven retirement. */
int drv_i915_engines_start(struct i915_device *device);
void drv_i915_engines_stop(struct i915_device *device);
int drv_i915_engine_reset(struct i915_engine *engine);
int drv_i915_engine_recover(struct i915_engine *engine, struct i915_session *session, int error);
void drv_i915_engine_interrupt(struct i915_device *device, unsigned index, uint16_t sources);
unsigned drv_i915_engine_idle(struct i915_engine *engine);

/* lrc.c: context images, descriptors, submission and status-buffer decoding. */
int drv_i915_lrc_create(struct i915_device *device, struct i915_engine *engine, struct i915_ppgtt *vm, uint32_t sw_id, struct i915_context *context);
void drv_i915_lrc_destroy(struct i915_device *device, struct i915_context *context);
void drv_i915_lrc_submit(struct i915_context *context);
void drv_i915_lrc_reset_csb(struct i915_engine *engine);
unsigned drv_i915_lrc_csb_consume(struct i915_engine *engine, unsigned *promotions);
int drv_i915_lrc_ring_space(const struct i915_context *context, unsigned dwords);
void drv_i915_lrc_ring_emit(struct i915_context *context, const uint32_t *dwords, unsigned count);

/* request.c: request slots, command emission, submission and retirement. */
int drv_i915_request_alloc(struct i915_engine *engine, struct i915_session *session, struct drv_gpu_completion *completion, struct i915_request **result);
void drv_i915_request_release(struct i915_engine *engine, struct i915_request *request);
void drv_i915_request_queue(struct i915_engine *engine, struct i915_request *request);
void drv_i915_request_kick(struct i915_engine *engine);
void drv_i915_request_retire(struct i915_engine *engine, struct i915_request **retired);
void drv_i915_request_fail(struct i915_engine *engine, struct i915_session *session, int error, struct i915_request **retired);
void drv_i915_request_complete_list(struct i915_engine *engine, struct i915_request *retired);

/* selftest.c: attach-time smoke test of BCS0 when the build enables it. */
int drv_i915_selftest(struct i915_device *device);
int drv_i915_clear_selftest(struct i915_device *device);

/* i915.c: native stream validation, exposed for the host fixture. */
int drv_i915_stream_parse(const void *buffer, uint32_t bytes, struct i915_stream *stream);

/* irq.c: MSI ownership and the Gen11 GT interrupt handler. */
int drv_i915_irq_start(struct i915_device *device);
int drv_i915_irq_stop(struct i915_device *device);
void drv_i915_irq_reset(struct i915_device *device);
int drv_i915_irq_handler(void *argument);

#endif
