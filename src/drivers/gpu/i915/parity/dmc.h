/*
 * WS031 Linux-parity — DMC firmware parse (F1) + load (F2) + async lifecycle (F3).
 *
 * Faithful port of intel_dmc.c for ADL-P (display version 13).  This header
 * covers the parse: the firmware blob (from the read-only provider) is validated
 * (CSS / package / per-DMC headers), the entry matching the display stepping is
 * selected for each DMC id, and its payload is copied into DEVICE-OWNED storage
 * so it outlives the released firmware handle.  Length units differ per header
 * (CSS/package/v3 in dwords, v1 in bytes) and the entry offset is relative to the
 * end of the CSS+package, exactly as the reference.
 */
#ifndef PARITY_DMC_H
#define PARITY_DMC_H

#include <stdint.h>
#include "backend_sync.h"

enum parity_dmc_id {
	PARITY_DMC_FW_MAIN = 0,
	PARITY_DMC_FW_PIPEA,
	PARITY_DMC_FW_PIPEB,
	PARITY_DMC_FW_PIPEC,
	PARITY_DMC_FW_PIPED,
	PARITY_DMC_FW_MAX
};

#define PARITY_DMC_MAX_MMIO 20   /* DMC_V3_MAX_MMIO_COUNT */

struct parity_dmc_info {
	int present;               /* an fw_info entry was selected for this id */
	uint32_t dmc_offset;       /* dwords (fw_info.offset), relative to CSS+package end */
	uint32_t mmio_count;
	uint32_t mmioaddr[PARITY_DMC_MAX_MMIO];
	uint32_t mmiodata[PARITY_DMC_MAX_MMIO];
	uint32_t start_mmioaddr;   /* program RAM start MMIO (v3) / DMC_V1_MMIO_START (v1) */
	uint32_t dmc_fw_size;      /* dwords (program size, excl. header) */
	const uint8_t *payload;    /* device-owned copy; NULL until saved */
	uint32_t payload_size;     /* bytes */
	int header_ver;            /* 1 or 3 */
};

struct parity_dmc {
	uint32_t version;          /* CSS version (major<<16 | minor) */
	uint32_t max_fw_size;      /* per-platform ceiling (bytes) */
	int display_ver;           /* 13 for ADL-P */
	char stepping;             /* display stepping char, e.g. 'D' */
	char substepping;          /* e.g. '0' */
	struct parity_dmc_info dmc_info[PARITY_DMC_FW_MAX];
	/* Diagnostics captured by the parse (for the GPU-free tests). */
	uint32_t css_header_len_bytes;
	uint32_t package_header_ver;
	uint32_t num_entries;
	int truncated;             /* set on a refused (truncated/invalid) blob */
	/* F2 load diagnostics. */
	unsigned payload_writes;   /* DMC_PROGRAM DWORD writes performed */
	unsigned aux_writes;       /* trailing per-DMC MMIO writes */
	unsigned evt_disable_writes; /* event-handler CTL+HTP disable writes */
	int load_seq_completed;    /* 1 only after the WHOLE sequence (incl. post) ran */
	uint64_t psum;             /* order-sensitive checksum of payload (addr,val) */
	uint64_t asum;             /* order-sensitive checksum of aux (addr,transformed val) */
};

/*
 * Parse the DMC firmware.  display_ver/stepping/substepping must be set on `dmc`
 * before the call (parity_dmc_prepare does this).  Returns 0 if the MAIN payload
 * was saved, or -errno (no MAIN).  Payloads are copied into device-owned storage;
 * `data` may be released afterwards.
 */
int parity_parse_dmc_fw(struct parity_dmc *dmc, const uint8_t *data, unsigned size);

/* Set display_ver + stepping and clear the info before a parse. */
void parity_dmc_prepare(struct parity_dmc *dmc, int display_ver,
	char stepping, char substepping);

/*
 * intel_dmc_load_program(): pre clock-gating WA -> disable event handlers ->
 * preempt-off payload writes (DMC_PROGRAM = start_mmioaddr + i*4) -> trailing
 * per-DMC MMIO (via dmc_mmiodata transform) -> dc_state=0 -> DC_STATE_DEBUG ->
 * post clock-gating WA.  *dc_state_out (optional) receives the reset dc_state.
 */
struct osdep_mmio;
void parity_intel_dmc_load_program(struct parity_dmc *dmc, struct osdep_mmio *m,
	uint32_t *dc_state_out);

/* Release the device-owned payload storage (fini). */
void parity_dmc_parse_reset(struct parity_dmc *dmc);

/* MAIN payload present (intel_dmc_has_payload). */
int parity_dmc_has_payload(const struct parity_dmc *dmc);

struct osdep_mmio;
struct parity_power_domains;
struct parity_pw_ctx;

/*
 * DMC async lifecycle (F3).  intel_dmc_init takes the DMC's OWN POWER_DOMAIN_INIT
 * reference (distinct from the display-core parent's), prepares the state and
 * queues the load worker; the worker acquires the firmware, parses, loads and,
 * on success, releases the DMC reference; fini flushes the worker (flush, not
 * cancel), releases any still-held DMC reference, and frees the payload arena.
 */
struct parity_dmc_dev {
	struct parity_dmc dmc;
	struct osdep_mmio *m;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *pwc;
	struct parity_kworkqueue *wq;
	const char *fw_path;
	struct parity_kwork work;
	int dmc_wakeref_held;          /* DMC's own INIT reference is held */
	uint32_t dc_state;
	/* diagnostics (kept distinct, per the reference). */
	int work_submitted;
	int worker_started;
	int firmware_acquired;
	int fallback_requested;
	int main_payload_present;      /* NOT the same as load_seq_completed */
	int load_seq_completed_flag;
	int first_fault;
};

void parity_intel_dmc_init(struct parity_dmc_dev *d, struct parity_kworkqueue *wq,
	struct osdep_mmio *m, struct parity_power_domains *pd, struct parity_pw_ctx *pwc,
	int display_ver, char stepping, char substepping, const char *fw_path);

/*
 * Test-only hooks (production leaves them 0):
 *  - parity_dmc_test_pause: the worker parks (yielding) AFTER the parse and
 *    BEFORE the preempt-off payload region while this is set, so a fini can be
 *    started from another thread against a running worker.
 *  - parity_dmc_test_fault_at: when non-zero, the payload loop aborts once this
 *    many DWORDs were written (simulating an adaptation-layer fault): preemption
 *    is restored, load_seq_completed stays 0, the DMC reference stays held.
 */
extern volatile int parity_dmc_test_pause;
extern volatile unsigned parity_dmc_test_fault_at;

/* Flush the worker (not cancel), release any held DMC reference, free the arena. */
void parity_intel_dmc_fini(struct parity_dmc_dev *d, uint64_t deadline);

#endif /* PARITY_DMC_H */
