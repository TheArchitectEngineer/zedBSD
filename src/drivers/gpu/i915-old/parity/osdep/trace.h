/*
 * WS031 Linux-parity OS adaptation layer — execution trace.
 *
 * A fixed-size in-memory ring of structured records.  The port records WHICH
 * processing actually ran (entry/exit, resource acquire/release, DMA map/unmap,
 * sync, worker lifecycle), separately from what the source statically calls, so
 * a missing log line is never read as "the code did not run".  Overflow is
 * itself recorded (dropped count) for the same reason.
 *
 * No console spew: the ring is dumped on demand.  No new MMIO reads are added
 * for measurement; only values the real processing already produced are logged.
 */
#ifndef PARITY_OSDEP_TRACE_H
#define PARITY_OSDEP_TRACE_H

#include <stdint.h>

enum osdep_trace_op {
	OSDEP_TR_ENTRY = 0,   /* function entry */
	OSDEP_TR_EXIT,        /* function exit (arg0 = return value) */
	OSDEP_TR_ACQUIRE,     /* resource acquired (arg0 = resource_id) */
	OSDEP_TR_RELEASE,     /* resource released (arg0 = resource_id) */
	OSDEP_TR_MAP,         /* DMA map (arg0 = cpu_phys, arg1 = dma_addr) */
	OSDEP_TR_UNMAP,       /* DMA unmap (arg0 = dma_addr) */
	OSDEP_TR_SYNC_DEV,    /* sync_for_device */
	OSDEP_TR_SYNC_CPU,    /* sync_for_cpu */
	OSDEP_TR_WORK_ENQ,    /* worker enqueue */
	OSDEP_TR_WORK_BEGIN,  /* worker begin */
	OSDEP_TR_WORK_END,    /* worker end */
	OSDEP_TR_WORK_CANCEL, /* worker cancel */
	OSDEP_TR_UNIMPL,      /* reached an unimplemented dependency (dev build stops before publish) */
	OSDEP_TR_FAIL,        /* an operation actually failed (arg0 = errno/return) */
	OSDEP_TR_NOTE,        /* free-form marker */
};

struct osdep_trace_record {
	uint32_t seq;         /* monotonic sequence; gaps mean records were dropped */
	uint16_t stage;       /* phase id, e.g. P0..P11 encoded, or 0 */
	uint16_t op;          /* enum osdep_trace_op */
	const char *what;     /* static string: function or operation name (not freed) */
	uint64_t arg0;
	uint64_t arg1;
};

/*
 * 1024 records = 32 KiB.  Rings are static objects (never automatic: a kernel
 * thread stack is 16 KiB) and the post-mortem dump keeps the last 256, so a
 * larger ring only costs .bss -- and .bss growth already made the UEFI loader
 * fail to place the kernel once.
 */
#ifndef OSDEP_TRACE_CAPACITY
#define OSDEP_TRACE_CAPACITY 1024u
#endif

struct osdep_trace {
	struct osdep_trace_record records[OSDEP_TRACE_CAPACITY];
	uint32_t next;        /* next write index (mod capacity) */
	uint32_t seq;         /* total records ever emitted */
	uint32_t dropped;     /* records overwritten before being read (overflow evidence) */
	int wrapped;          /* the ring wrapped at least once */
};

void osdep_trace_init(struct osdep_trace *t);
void osdep_trace_emit(struct osdep_trace *t, uint16_t stage, uint16_t op,
		      const char *what, uint64_t arg0, uint64_t arg1);
/* Number of live records currently retrievable. */
uint32_t osdep_trace_count(const struct osdep_trace *t);
/* Copy up to max live records (oldest first) into out; returns copied count. */
uint32_t osdep_trace_snapshot(const struct osdep_trace *t,
			      struct osdep_trace_record *out, uint32_t max);
const char *osdep_trace_op_name(uint16_t op);

#endif /* PARITY_OSDEP_TRACE_H */
