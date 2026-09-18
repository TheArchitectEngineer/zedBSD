/* WS031 Linux-parity OS adaptation layer — MMIO / uncore (see mmio.h). */
#include "mmio.h"

#ifndef OSDEP_EINVAL
#define OSDEP_EINVAL 22
#define OSDEP_EBUSY  16
#define OSDEP_ETIME  62
#endif

static void
tr(struct osdep_mmio *u, uint16_t op, const char *what, uint64_t a0, uint64_t a1)
{
	if (u->trace != 0)
		osdep_trace_emit(u->trace, 0u, op, what, a0, a1);
}

void
osdep_mmio_init(struct osdep_mmio *u, const struct osdep_mmio_backend *backend,
		void *priv, const struct osdep_mmio_range *ranges, unsigned range_count,
		struct osdep_trace *trace)
{
	int d;

	u->backend = backend;
	u->priv = priv;
	u->trace = trace;
	u->ranges = ranges;
	u->range_count = range_count;
	for (d = 0; d < OSDEP_FW_DOMAIN_COUNT; d++) {
		u->fw_count[d] = 0;
		u->fw_awake[d] = 0;
	}
	u->fw_ack_timeouts = 0u;
	u->fw_underflow = 0;
	u->mcr_locked = 0;
	u->mcr_steer = 0u;
}

int
osdep_mmio_domain_of(const struct osdep_mmio *u, uint32_t offset)
{
	unsigned i;

	for (i = 0u; i < u->range_count; i++)
		if (offset >= u->ranges[i].start && offset <= u->ranges[i].end)
			return u->ranges[i].domain;
	return -1;   /* not in a forcewake range: always-on */
}

int
osdep_fw_get(struct osdep_mmio *u, int domain)
{
	unsigned poll;

	if (domain < 0 || domain >= OSDEP_FW_DOMAIN_COUNT)
		return -OSDEP_EINVAL;

	tr(u, OSDEP_TR_ACQUIRE, "forcewake", (uint64_t)domain, (uint64_t)(u->fw_count[domain] + 1));
	u->fw_count[domain]++;
	if (u->fw_count[domain] > 1)
		return 0;   /* nested: already awake */

	/* 0 -> 1: wake the domain and wait for the ACK. */
	u->backend->fw_request(u->priv, domain, 1);
	for (poll = 0u; poll < OSDEP_FW_ACK_POLLS; poll++) {
		if (u->backend->fw_ack(u->priv, domain)) {
			u->fw_awake[domain] = 1;
			return 0;
		}
	}
	/* ACK never came: undo the reference so the count reflects reality. */
	u->fw_ack_timeouts++;
	u->fw_count[domain]--;
	tr(u, OSDEP_TR_FAIL, "forcewake_ack", (uint64_t)domain, (uint64_t)(-OSDEP_ETIME));
	return -OSDEP_ETIME;
}

int
osdep_fw_put(struct osdep_mmio *u, int domain)
{
	if (domain < 0 || domain >= OSDEP_FW_DOMAIN_COUNT)
		return -OSDEP_EINVAL;
	if (u->fw_count[domain] == 0) {
		/* Over-put: a real bug in the caller's balance.  Record, refuse. */
		u->fw_underflow = 1;
		tr(u, OSDEP_TR_FAIL, "forcewake_underflow", (uint64_t)domain, 0u);
		return -OSDEP_EINVAL;
	}

	tr(u, OSDEP_TR_RELEASE, "forcewake", (uint64_t)domain, (uint64_t)(u->fw_count[domain] - 1));
	u->fw_count[domain]--;
	if (u->fw_count[domain] == 0) {
		/* 1 -> 0: let the domain sleep. */
		u->backend->fw_request(u->priv, domain, 0);
		u->fw_awake[domain] = 0;
	}
	return 0;
}

int
osdep_fw_is_held(const struct osdep_mmio *u, int domain)
{
	if (domain < 0 || domain >= OSDEP_FW_DOMAIN_COUNT)
		return 0;
	return u->fw_count[domain] > 0;
}

uint32_t
osdep_mmio_read32_auto(struct osdep_mmio *u, uint32_t offset)
{
	int domain = osdep_mmio_domain_of(u, offset);
	uint32_t v;

	/* Normal access takes the register's forcewake for the duration itself. */
	if (domain >= 0)
		(void)osdep_fw_get(u, domain);
	v = u->backend->raw_read32(u->priv, offset);
	if (domain >= 0)
		(void)osdep_fw_put(u, domain);
	return v;
}

void
osdep_mmio_write32_auto(struct osdep_mmio *u, uint32_t offset, uint32_t value)
{
	int domain = osdep_mmio_domain_of(u, offset);

	if (domain >= 0)
		(void)osdep_fw_get(u, domain);
	u->backend->raw_write32(u->priv, offset, value);
	if (domain >= 0)
		(void)osdep_fw_put(u, domain);
}

uint32_t
osdep_mmio_read32(struct osdep_mmio *u, uint32_t offset)
{
	int domain = osdep_mmio_domain_of(u, offset);

	/* A forcewaked register read without the domain held returns garbage on HW. */
	if (domain >= 0 && !osdep_fw_is_held(u, domain)) {
		tr(u, OSDEP_TR_FAIL, "mmio_read_no_fw", offset, (uint64_t)domain);
		return 0xffffffffu;
	}
	return u->backend->raw_read32(u->priv, offset);
}

void
osdep_mmio_write32(struct osdep_mmio *u, uint32_t offset, uint32_t value)
{
	int domain = osdep_mmio_domain_of(u, offset);

	if (domain >= 0 && !osdep_fw_is_held(u, domain)) {
		tr(u, OSDEP_TR_FAIL, "mmio_write_no_fw", offset, (uint64_t)domain);
		return;
	}
	u->backend->raw_write32(u->priv, offset, value);
}

uint32_t
osdep_mmio_raw_read32(struct osdep_mmio *u, uint32_t offset)
{
	return u->backend->raw_read32(u->priv, offset);
}

void
osdep_mmio_raw_write32(struct osdep_mmio *u, uint32_t offset, uint32_t value)
{
	u->backend->raw_write32(u->priv, offset, value);
}

void
osdep_mmio_posting_read32(struct osdep_mmio *u, uint32_t offset)
{
	/* The value is intentionally discarded; the read exists to post the write. */
	(void)u->backend->raw_read32(u->priv, offset);
	tr(u, OSDEP_TR_NOTE, "posting_read", offset, 0u);
}

void
osdep_mmio_write32_masked(struct osdep_mmio *u, uint32_t offset, uint32_t mask, uint32_t value)
{
	int domain = osdep_mmio_domain_of(u, offset);
	uint32_t old;
	uint32_t neww;

	if (domain >= 0 && !osdep_fw_is_held(u, domain)) {
		tr(u, OSDEP_TR_FAIL, "mmio_rmw_no_fw", offset, (uint64_t)domain);
		return;
	}
	old = u->backend->raw_read32(u->priv, offset);
	neww = (old & ~mask) | (value & mask);
	u->backend->raw_write32(u->priv, offset, neww);
}

void
osdep_mmio_write32_mask_enable(struct osdep_mmio *u, uint32_t offset, uint32_t masked_word)
{
	int domain = osdep_mmio_domain_of(u, offset);

	if (domain >= 0 && !osdep_fw_is_held(u, domain)) {
		tr(u, OSDEP_TR_FAIL, "mmio_maskwrite_no_fw", offset, (uint64_t)domain);
		return;
	}
	/* Single write; the mask lives in the upper half.  No read-modify-write. */
	u->backend->raw_write32(u->priv, offset, masked_word);
}

int
osdep_mcr_lock(struct osdep_mmio *u, uint32_t steer)
{
	if (u->mcr_locked) {
		/* Exclusive: a second steer while locked would corrupt the first. */
		tr(u, OSDEP_TR_FAIL, "mcr_reentry", steer, u->mcr_steer);
		return -OSDEP_EBUSY;
	}
	u->mcr_locked = 1;
	u->mcr_steer = steer;
	tr(u, OSDEP_TR_ACQUIRE, "mcr_steer", steer, 0u);
	return 0;
}

void
osdep_mcr_unlock(struct osdep_mmio *u)
{
	if (!u->mcr_locked)
		return;
	u->mcr_locked = 0;
	tr(u, OSDEP_TR_RELEASE, "mcr_steer", u->mcr_steer, 0u);
}

int
osdep_mcr_is_locked(const struct osdep_mmio *u)
{
	return u->mcr_locked;
}
