/* Mock MMIO backend (test only) — see mock_mmio.h. */
#include "mock_mmio.h"

/* RCS register bands are render-forcewaked; the 0x9xxx band is GT; else always-on. */
const struct osdep_mmio_range mock_mmio_ranges[] = {
	{ 0x2000u, 0x2fffu, OSDEP_FW_RENDER },
	{ 0xe000u, 0xefffu, OSDEP_FW_RENDER },
	{ 0x9000u, 0x9fffu, OSDEP_FW_GT },
};
const unsigned mock_mmio_range_count =
	sizeof(mock_mmio_ranges) / sizeof(mock_mmio_ranges[0]);

static struct mock_mmio *g_self; /* single instance for the test */

static int
find(struct mock_mmio *m, uint32_t off)
{
	int i;

	for (i = 0; i < MOCK_MMIO_REGS; i++)
		if (m->regs[i].used && m->regs[i].off == off)
			return i;
	return -1;
}

static int
slot(struct mock_mmio *m, uint32_t off)
{
	int i = find(m, off);
	if (i >= 0)
		return i;
	for (i = 0; i < MOCK_MMIO_REGS; i++)
		if (!m->regs[i].used) {
			m->regs[i].used = 1;
			m->regs[i].off = off;
			m->regs[i].val = 0u;
			return i;
		}
	return -1;
}

static uint32_t
m_raw_read32(void *priv, uint32_t offset)
{
	struct mock_mmio *m = (struct mock_mmio *)priv;
	int i;

	m->read_calls++;
	i = find(m, offset);
	return i >= 0 ? m->regs[i].val : 0u;
}

static void
m_raw_write32(void *priv, uint32_t offset, uint32_t value)
{
	struct mock_mmio *m = (struct mock_mmio *)priv;
	int i;

	m->write_calls++;
	i = slot(m, offset);
	if (i >= 0)
		m->regs[i].val = value;
}

static void
m_fw_request(void *priv, int domain, int wake)
{
	struct mock_mmio *m = (struct mock_mmio *)priv;

	m->fw_wake[domain] = wake;
	m->fw_request_calls[domain]++;
}

static int
m_fw_ack(void *priv, int domain)
{
	struct mock_mmio *m = (struct mock_mmio *)priv;

	if (m->fw_never_ack[domain])
		return 0;
	return m->fw_wake[domain] ? 1 : 0;
}

const struct osdep_mmio_backend *
mock_mmio_backend(void)
{
	static const struct osdep_mmio_backend backend = {
		"mock-mmio",
		m_raw_read32,
		m_raw_write32,
		m_fw_request,
		m_fw_ack,
	};
	return &backend;
}

void
mock_mmio_reset(struct mock_mmio *m)
{
	int i;

	g_self = m;
	for (i = 0; i < MOCK_MMIO_REGS; i++) {
		m->regs[i].used = 0;
		m->regs[i].off = 0u;
		m->regs[i].val = 0u;
	}
	for (i = 0; i < OSDEP_FW_DOMAIN_COUNT; i++) {
		m->fw_wake[i] = 0;
		m->fw_never_ack[i] = 0;
		m->fw_request_calls[i] = 0;
	}
	m->read_calls = 0;
	m->write_calls = 0;
}

void
mock_mmio_preset(struct mock_mmio *m, uint32_t off, uint32_t val)
{
	int i = slot(m, off);
	if (i >= 0)
		m->regs[i].val = val;
}

uint32_t
mock_mmio_peek(struct mock_mmio *m, uint32_t off)
{
	int i = find(m, off);
	return i >= 0 ? m->regs[i].val : 0u;
}
