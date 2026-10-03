/*
 * WS031 Linux-parity — common GEN6+ PCODE (sideband) mailbox.
 *
 * Port of intel_pcode.c's transaction core (__snb_pcode_rw / snb_pcode_read).
 * The device-owned sb_lock (a mutex) serialises every PCODE transaction; the
 * fast stage is an atomic register poll and, if it does not complete, the slow
 * stage is a sleepable poll (hence a mutex, not a spinlock).  Used by both the
 * DRAM detection and the display-bandwidth branches.
 */
#ifndef PARITY_PCODE_H
#define PARITY_PCODE_H

#include <stdint.h>

struct osdep_mmio;
struct mutex;

/*
 * snb_pcode_read equivalent: read a PCODE mailbox command's response under the
 * caller-owned sb_lock.  Returns 0 or a negative errno (mailbox status,
 * -ETIMEDOUT, or -EAGAIN) per the reference's gen7 status conversion.
 */
int parity_pcode_read(struct mutex *sb_lock, struct osdep_mmio *m,
	uint32_t mbox, uint32_t *val, uint32_t *val1);

/* snb_pcode_write_timeout / snb_pcode_write: a single PCODE write. */
int parity_snb_pcode_write_timeout(struct mutex *sb_lock, struct osdep_mmio *m,
	uint32_t mbox, uint32_t val, unsigned fast_us, unsigned slow_ms);
int parity_snb_pcode_write(struct mutex *sb_lock, struct osdep_mmio *m,
	uint32_t mbox, uint32_t val);

/*
 * skl_pcode_request: send a request and re-request until (reply & reply_mask)
 * == reply within timeout_base_ms, then a bounded retry.  Returns 0, a PCODE
 * status errno, -ETIMEDOUT, or -EIO (time-base anomaly).
 */
int parity_skl_pcode_request(struct mutex *sb_lock, struct osdep_mmio *m,
	uint32_t mbox, uint32_t request, uint32_t reply_mask, uint32_t reply,
	int timeout_base_ms);

#endif /* PARITY_PCODE_H */
