/* Mock MMIO backend (test only) — a small register file + forcewake ack model. */
#ifndef PARITY_TESTS_MOCK_MMIO_H
#define PARITY_TESTS_MOCK_MMIO_H

#include "../osdep/mmio.h"

#define MOCK_MMIO_REGS 256

struct mock_mmio {
	struct { uint32_t off; uint32_t val; int used; } regs[MOCK_MMIO_REGS];
	int fw_wake[OSDEP_FW_DOMAIN_COUNT];         /* last requested wake state */
	int fw_never_ack[OSDEP_FW_DOMAIN_COUNT];    /* force an ACK timeout */
	int fw_request_calls[OSDEP_FW_DOMAIN_COUNT];
	int read_calls;
	int write_calls;
};

const struct osdep_mmio_backend *mock_mmio_backend(void);
void mock_mmio_reset(struct mock_mmio *m);
void mock_mmio_preset(struct mock_mmio *m, uint32_t off, uint32_t val);
uint32_t mock_mmio_peek(struct mock_mmio *m, uint32_t off);

/* A small forcewake range table for tests. */
extern const struct osdep_mmio_range mock_mmio_ranges[];
extern const unsigned mock_mmio_range_count;

#endif
