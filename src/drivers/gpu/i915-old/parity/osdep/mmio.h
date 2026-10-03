/*
 * WS031 Linux-parity OS adaptation layer — MMIO / uncore.
 *
 * Keeps the distinctions the reference uncore keeps and the old code flattened:
 *
 *   - Normal register access vs RAW access.  Normal 32-bit read/write assert that
 *     the register's forcewake domain is currently held; raw access is for the
 *     narrow cases where forcewake is guaranteed by the caller (init/reset).
 *   - Explicit forcewake with a reference count and an ACK handshake.  The domain
 *     is woken on the 0->1 transition (and its ACK awaited) and released on the
 *     1->0 transition.  Nested get/put is counted; an over-put is detected.
 *   - A posting read is a distinct operation (flush a prior write), not a plain read.
 *   - Masked (RMW) writes use the Gen mask-in-high-half form as one operation.
 *   - MCR (multicast/replicated) steering is a locked, exclusive section.
 *
 * Actual bus access and the forcewake request/ack registers live behind a
 * backend vtable, so the contract is testable GPU-free with a mock register file.
 */
#ifndef PARITY_OSDEP_MMIO_H
#define PARITY_OSDEP_MMIO_H

#include <stdint.h>
#include "trace.h"

/*
 * Forcewake domains this port uses.  Gen12 splits the media engines off into
 * their own domains: a VCS/VECS register reached while only RENDER and GT are
 * held is NOT covered, which is exactly the silent failure (reads that decode
 * as "everything off", writes that are dropped) this layer exists to catch.
 * The names and the per-instance numbering follow the reference:
 * FORCEWAKE_MEDIA_VDBOX_GEN11(n) / FORCEWAKE_MEDIA_VEBOX_GEN11(n).
 */
enum osdep_fw_domain {
	OSDEP_FW_RENDER = 0,
	OSDEP_FW_GT,
	OSDEP_FW_MEDIA_VDBOX0,   /* VCS0  (0x1c0000) */
	OSDEP_FW_MEDIA_VDBOX2,   /* VCS2  (0x1d0000) */
	OSDEP_FW_MEDIA_VEBOX0,   /* VECS0 (0x1c8000) */
	OSDEP_FW_DOMAIN_COUNT
};

/* Which forcewake domain a register offset belongs to (0 = always-on / no forcewake). */
struct osdep_mmio_range {
	uint32_t start;
	uint32_t end;
	int domain;          /* enum osdep_fw_domain, or -1 for always-on */
};

struct osdep_mmio_backend {
	const char *name;
	/* Raw register access (no forcewake bookkeeping). */
	uint32_t (*raw_read32)(void *priv, uint32_t offset);
	void (*raw_write32)(void *priv, uint32_t offset, uint32_t value);
	/* Forcewake: write the request for `wake` (1=wake,0=sleep) and return the ACK bit. */
	void (*fw_request)(void *priv, int domain, int wake);
	int  (*fw_ack)(void *priv, int domain);   /* non-zero once the domain acked `wake` */
};

#define OSDEP_FW_ACK_POLLS 4096u

struct osdep_mmio {
	const struct osdep_mmio_backend *backend;
	void *priv;
	struct osdep_trace *trace;

	const struct osdep_mmio_range *ranges;
	unsigned range_count;

	int fw_count[OSDEP_FW_DOMAIN_COUNT];   /* forcewake reference counts */
	int fw_awake[OSDEP_FW_DOMAIN_COUNT];   /* domain currently awake+acked */
	uint32_t fw_ack_timeouts;              /* diagnostics */
	int fw_underflow;                      /* an over-put was attempted */

	int mcr_locked;                        /* MCR steering section held */
	uint32_t mcr_steer;                    /* current steering value while locked */
};

void osdep_mmio_init(struct osdep_mmio *u, const struct osdep_mmio_backend *backend,
		     void *priv, const struct osdep_mmio_range *ranges, unsigned range_count,
		     struct osdep_trace *trace);

/* Which domain owns `offset` (-1 = always-on). */
int osdep_mmio_domain_of(const struct osdep_mmio *u, uint32_t offset);

/* Forcewake get/put with refcount + ACK.  get returns 0 / -errno (ACK timeout). */
int osdep_fw_get(struct osdep_mmio *u, int domain);
int osdep_fw_put(struct osdep_mmio *u, int domain);
int osdep_fw_is_held(const struct osdep_mmio *u, int domain);

/*
 * Three access layers, matching the reference uncore:
 *   auto  — normal access; the layer takes/releases the register's forcewake for
 *           the duration (like intel_uncore_read/write).  Caller holds nothing.
 *   held  — the caller already holds forcewake across a sequence; access asserts it.
 *   raw   — no forcewake bookkeeping at all (init/reset paths only).
 */
uint32_t osdep_mmio_read32_auto(struct osdep_mmio *u, uint32_t offset);
void osdep_mmio_write32_auto(struct osdep_mmio *u, uint32_t offset, uint32_t value);

/* Held access: the register's domain must currently be held (else sentinel / trace FAIL). */
uint32_t osdep_mmio_read32(struct osdep_mmio *u, uint32_t offset);
void osdep_mmio_write32(struct osdep_mmio *u, uint32_t offset, uint32_t value);

/* Raw access: forcewake guaranteed by caller (init/reset paths only). */
uint32_t osdep_mmio_raw_read32(struct osdep_mmio *u, uint32_t offset);
void osdep_mmio_raw_write32(struct osdep_mmio *u, uint32_t offset, uint32_t value);

/* Posting read: issue a read whose only purpose is to flush a prior write. */
void osdep_mmio_posting_read32(struct osdep_mmio *u, uint32_t offset);

/*
 * Masked RMW: read, modify only the masked bits, write back.  A true read-modify-
 * write (two bus operations).  For registers WITHOUT the Intel write-mask form.
 */
void osdep_mmio_write32_masked(struct osdep_mmio *u, uint32_t offset, uint32_t mask, uint32_t value);

/*
 * Intel masked write: a SINGLE 32-bit write whose upper 16 bits are the write-
 * enable mask and lower 16 the value (the word a _MASKED_BIT_ENABLE()-style macro
 * produced).  No read.  The hardware applies the mask; this must not be confused
 * with osdep_mmio_write32_masked (which does an RMW).
 */
void osdep_mmio_write32_mask_enable(struct osdep_mmio *u, uint32_t offset, uint32_t masked_word);

/* MCR steering: an exclusive locked section.  steer sets the target; access is multicast otherwise. */
int  osdep_mcr_lock(struct osdep_mmio *u, uint32_t steer);
void osdep_mcr_unlock(struct osdep_mmio *u);
int  osdep_mcr_is_locked(const struct osdep_mmio *u);

#endif /* PARITY_OSDEP_MMIO_H */
