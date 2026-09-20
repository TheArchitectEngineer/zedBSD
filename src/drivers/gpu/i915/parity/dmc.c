/*
 * WS031 Linux-parity — DMC firmware parse (F1) for ADL-P (see dmc.h).
 *
 * Direct port of intel_dmc.c's parse_dmc_fw / parse_dmc_fw_css /
 * parse_dmc_fw_package / parse_dmc_fw_header / dmc_set_fw_offset for display
 * version 13.  Payloads are copied into a device-owned arena so they outlive the
 * released firmware handle.  No hardware access here (that is F2).
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include "dmc.h"
#include "osdep/mmio.h"
#include <kern/sched.h>
#include "osdep/firmware.h"
#include "power_domains.h"
#include "backend_sync.h"
#include <kern/clock.h>

volatile int parity_dmc_test_pause;
volatile unsigned parity_dmc_test_fault_at;

/* --- reference constants --- */
#define PACKAGE_MAX_FW_INFO_ENTRIES    20
#define PACKAGE_V2_MAX_FW_INFO_ENTRIES 32
#define DMC_V1_MAX_MMIO_COUNT          8
#define DMC_V3_MAX_MMIO_COUNT          20
#define DMC_V1_MMIO_START_RANGE        0x80000u
#define DMC_MMIO_START_RANGE           0x80000u
#define DMC_MMIO_END_RANGE             0x8FFFFu
#define TGL_MAIN_MMIO_START            0x8F000u
#define TGL_MAIN_MMIO_END              0x8FFFFu
#define ADLP_PIPE_MMIO_START           0x5F000u
#define ADLP_PIPE_MMIO_END             0x5FFFFu
/* display version 12 keeps one window per pipe payload (intel_dmc_regs.h, _PICK_EVEN(dmc_id - 1)) */
#define _TGL_PIPEA_MMIO_START          0x92000u
#define _TGL_PIPEA_MMIO_END            0x93FFFu
#define _TGL_PIPEB_MMIO_START          0x96000u
#define _TGL_PIPEB_MMIO_END            0x97FFFu
#define TGL_PIPE_MMIO_START(id) (_TGL_PIPEA_MMIO_START + (uint32_t)((id) - 1) * (_TGL_PIPEB_MMIO_START - _TGL_PIPEA_MMIO_START))
#define TGL_PIPE_MMIO_END(id)   (_TGL_PIPEA_MMIO_END + (uint32_t)((id) - 1) * (_TGL_PIPEB_MMIO_END - _TGL_PIPEA_MMIO_END))
#define DISPLAY_VER13_DMC_MAX_FW_SIZE  0x20000u
#define ICL_DMC_MAX_FW_SIZE            0x6000u
#define DISPLAY_VER12_DMC_MAX_FW_SIZE  ICL_DMC_MAX_FW_SIZE

/* --- packed on-disk firmware layout (intel_dmc.c) --- */
struct css_header {
	uint32_t module_type, header_len, header_ver, module_id, module_vendor, date, size;
	uint32_t key_size, modulus_size, exponent_size, reserved1[12], version, reserved2[8];
	uint32_t kernel_header_info;
} __attribute__((packed));

struct fw_info {
	uint8_t reserved1, dmc_id;
	char stepping, substepping;
	uint32_t offset, reserved2;
} __attribute__((packed));

struct package_header {
	uint8_t header_len, header_ver, reserved[10];
	uint32_t num_entries;
} __attribute__((packed));

struct dmc_header_base {
	uint32_t signature;
	uint8_t header_len, header_ver;
	uint16_t dmcc_ver;
	uint32_t project, fw_size, fw_version;
} __attribute__((packed));

struct dmc_header_v1 {
	struct dmc_header_base base;
	uint32_t mmio_count;
	uint32_t mmioaddr[DMC_V1_MAX_MMIO_COUNT];
	uint32_t mmiodata[DMC_V1_MAX_MMIO_COUNT];
	char dfile[32];
	uint32_t reserved1[2];
} __attribute__((packed));

struct dmc_header_v3 {
	struct dmc_header_base base;
	uint32_t start_mmioaddr;
	uint32_t reserved[9];
	char dfile[32];
	uint32_t mmio_count;
	uint32_t mmioaddr[DMC_V3_MAX_MMIO_COUNT];
	uint32_t mmiodata[DMC_V3_MAX_MMIO_COUNT];
} __attribute__((packed));

/* Device-owned payload arena (separate from the firmware blob). */
static uint8_t g_dmc_arena[DISPLAY_VER13_DMC_MAX_FW_SIZE];
static unsigned g_dmc_arena_used;

static const uint8_t *
arena_copy(const uint8_t *src, unsigned n)
{
	uint8_t *dst;
	unsigned i;

	if (n > sizeof(g_dmc_arena) - g_dmc_arena_used)
		return 0;   /* out of arena: treat like an allocation failure */
	dst = &g_dmc_arena[g_dmc_arena_used];
	for (i = 0u; i < n; i++)
		dst[i] = src[i];
	g_dmc_arena_used += n;
	return dst;
}

static int is_valid_dmc_id(int id) { return id >= PARITY_DMC_FW_MAIN && id < PARITY_DMC_FW_MAX; }

/* fw_info_matches_stepping(). */
static int
fw_info_matches_stepping(const struct fw_info *fi, char step, char sub)
{
	if ((fi->substepping == '*' && step == fi->stepping) ||
	    (step == fi->stepping && sub == fi->substepping) ||
	    (step == '*' && sub == fi->substepping) ||
	    (fi->stepping == '*' && fi->substepping == '*'))
		return 1;
	return 0;
}

/* dmc_set_fw_offset(): keep the FIRST matching entry per id (more specific first). */
static void
dmc_set_fw_offset(struct parity_dmc *dmc, const struct fw_info *fw_info,
	unsigned num_entries, char step, char sub, uint8_t package_ver)
{
	unsigned i;

	for (i = 0u; i < num_entries; i++) {
		int id = (package_ver <= 1) ? PARITY_DMC_FW_MAIN : (int)fw_info[i].dmc_id;

		if (!is_valid_dmc_id(id))
			continue;
		if (dmc->dmc_info[id].present)
			continue;   /* already found a (more specific) entry for this id */
		if (fw_info_matches_stepping(&fw_info[i], step, sub)) {
			dmc->dmc_info[id].present = 1;
			dmc->dmc_info[id].dmc_offset = fw_info[i].offset;
		}
	}
}

/* dmc_mmio_addr_sanity_check(). */
static int
mmio_addr_ok(struct parity_dmc *dmc, const uint32_t *addr, uint32_t count,
	int header_ver, int id)
{
	uint32_t lo, hi, i;

	if (header_ver == 1) {
		lo = DMC_MMIO_START_RANGE; hi = DMC_MMIO_END_RANGE;
	} else if (id == PARITY_DMC_FW_MAIN) {
		lo = TGL_MAIN_MMIO_START; hi = TGL_MAIN_MMIO_END;
	} else if (dmc->display_ver >= 13) {
		lo = ADLP_PIPE_MMIO_START; hi = ADLP_PIPE_MMIO_END;
	} else if (dmc->display_ver >= 12) {
		/* Tiger Lake: one window per pipe payload, not the single ADL-P one (E-126) */
		lo = TGL_PIPE_MMIO_START(id); hi = TGL_PIPE_MMIO_END(id);
	} else {
		/* XXX: unimplemented path -- displays before version 12 are not ported. */
		kern_logf("i915: parity dmc: XXX unknown mmio range for sanity check (display_ver=%d)\n",
			dmc->display_ver);
		return 0;
	}
	for (i = 0u; i < count; i++)
		if (addr[i] < lo || addr[i] > hi)
			return 0;
	return 1;
}

/* parse_dmc_fw_css(). */
static uint32_t
parse_css(struct parity_dmc *dmc, const uint8_t *data, unsigned rem)
{
	const struct css_header *css = (const struct css_header *)data;

	if (rem < sizeof(struct css_header))
		return 0;
	if (sizeof(struct css_header) != (uint32_t)css->header_len * 4u) {
		kern_logf("i915: parity dmc: wrong CSS header length (%u bytes)\n",
			(unsigned)css->header_len * 4u);
		return 0;
	}
	dmc->version = css->version;
	dmc->css_header_len_bytes = (uint32_t)css->header_len * 4u;
	return sizeof(struct css_header);
}

/* parse_dmc_fw_package(). */
static uint32_t
parse_package(struct parity_dmc *dmc, const uint8_t *data, unsigned rem)
{
	const struct package_header *ph = (const struct package_header *)data;
	uint32_t package_size = sizeof(struct package_header);
	uint32_t max_entries, num_entries;
	const struct fw_info *fi;

	if (rem < package_size)
		return 0;
	if (ph->header_ver == 1)
		max_entries = PACKAGE_MAX_FW_INFO_ENTRIES;
	else if (ph->header_ver == 2)
		max_entries = PACKAGE_V2_MAX_FW_INFO_ENTRIES;
	else {
		kern_logf("i915: parity dmc: unknown package header version %u\n", ph->header_ver);
		return 0;
	}
	package_size += max_entries * sizeof(struct fw_info);
	if (rem < package_size)
		return 0;
	if ((uint32_t)ph->header_len * 4u != package_size) {
		kern_logf("i915: parity dmc: wrong package header length (%u bytes)\n", package_size);
		return 0;
	}
	num_entries = ph->num_entries;
	if (num_entries > max_entries)
		num_entries = max_entries;

	dmc->package_header_ver = ph->header_ver;
	dmc->num_entries = num_entries;

	fi = (const struct fw_info *)(data + sizeof(struct package_header));
	dmc_set_fw_offset(dmc, fi, num_entries, dmc->stepping, dmc->substepping, ph->header_ver);
	return package_size;
}

/* parse_dmc_fw_header(): validate + save one DMC id's payload. */
static uint32_t
parse_header(struct parity_dmc *dmc, const uint8_t *data, unsigned rem, int id)
{
	const struct dmc_header_base *base = (const struct dmc_header_base *)data;
	struct parity_dmc_info *info = &dmc->dmc_info[id];
	unsigned header_len_bytes, dmc_header_size, payload_size, i;
	const uint32_t *mmioaddr, *mmiodata;
	uint32_t mmio_count, mmio_count_max, start_mmioaddr;

	if (rem < sizeof(struct dmc_header_base))
		goto truncated;

	if (base->header_ver == 3) {
		const struct dmc_header_v3 *v3 = (const struct dmc_header_v3 *)data;

		if (rem < sizeof(struct dmc_header_v3))
			goto truncated;
		mmioaddr = v3->mmioaddr; mmiodata = v3->mmiodata;
		mmio_count = v3->mmio_count; mmio_count_max = DMC_V3_MAX_MMIO_COUNT;
		header_len_bytes = (unsigned)base->header_len * 4u;   /* v3: dwords */
		start_mmioaddr = v3->start_mmioaddr;
		dmc_header_size = sizeof(struct dmc_header_v3);
	} else if (base->header_ver == 1) {
		const struct dmc_header_v1 *v1 = (const struct dmc_header_v1 *)data;

		if (rem < sizeof(struct dmc_header_v1))
			goto truncated;
		mmioaddr = v1->mmioaddr; mmiodata = v1->mmiodata;
		mmio_count = v1->mmio_count; mmio_count_max = DMC_V1_MAX_MMIO_COUNT;
		header_len_bytes = base->header_len;                  /* v1: bytes */
		start_mmioaddr = DMC_V1_MMIO_START_RANGE;
		dmc_header_size = sizeof(struct dmc_header_v1);
	} else {
		kern_logf("i915: parity dmc: unknown DMC fw header version %u\n", base->header_ver);
		return 0;
	}

	if (header_len_bytes != dmc_header_size) {
		kern_logf("i915: parity dmc: wrong dmc header length (%u bytes)\n", header_len_bytes);
		return 0;
	}
	if (mmio_count > mmio_count_max) {
		kern_logf("i915: parity dmc: wrong mmio count %u\n", mmio_count);
		return 0;
	}
	if (!mmio_addr_ok(dmc, mmioaddr, mmio_count, base->header_ver, id)) {
		/* say WHICH payload and WHICH address: a platform whose range is missing shows up here (E-126) */
		kern_logf("i915: parity dmc: wrong MMIO addresses (display_ver=%d id=%d header_ver=%d "
			"count=%u first=0x%05x last=0x%05x)\n", dmc->display_ver, id, base->header_ver,
			mmio_count, mmio_count != 0u ? mmioaddr[0] : 0u,
			mmio_count != 0u ? mmioaddr[mmio_count - 1u] : 0u);
		return 0;
	}

	for (i = 0u; i < mmio_count; i++) {
		info->mmioaddr[i] = mmioaddr[i];
		info->mmiodata[i] = mmiodata[i];
	}
	info->mmio_count = mmio_count;
	info->start_mmioaddr = start_mmioaddr;
	info->header_ver = base->header_ver;

	rem -= header_len_bytes;

	payload_size = base->fw_size * 4u;   /* fw_size is dwords */
	if (rem < payload_size)
		goto truncated;
	if (payload_size > dmc->max_fw_size) {
		kern_logf("i915: parity dmc: FW too big (%u bytes)\n", payload_size);
		return 0;
	}
	info->dmc_fw_size = base->fw_size;

	info->payload = arena_copy(data + header_len_bytes, payload_size);
	if (info->payload == 0)
		return 0;   /* allocation (arena) failure */
	info->payload_size = payload_size;

	return header_len_bytes + payload_size;

truncated:
	dmc->truncated = 1;
	kern_logf("i915: parity dmc: truncated DMC firmware, refusing\n");
	return 0;
}

void
parity_dmc_prepare(struct parity_dmc *dmc, int display_ver, char stepping, char substepping)
{
	unsigned i;

	for (i = 0u; i < sizeof(*dmc); i++)
		((char *)dmc)[i] = 0;
	dmc->display_ver = display_ver;
	dmc->stepping = stepping;
	dmc->substepping = substepping;
	/* the payload ceiling is per display version too (intel_dmc_init: E-126) */
	dmc->max_fw_size = display_ver >= 13 ? DISPLAY_VER13_DMC_MAX_FW_SIZE :
		DISPLAY_VER12_DMC_MAX_FW_SIZE;
	g_dmc_arena_used = 0u;   /* fresh device-owned payload storage */
}

int
parity_parse_dmc_fw(struct parity_dmc *dmc, const uint8_t *data, unsigned size)
{
	uint32_t readcount = 0, r, offset;
	int id;

	if (data == 0 || size == 0u)
		return -EINVAL;

	r = parse_css(dmc, data, size);
	if (r == 0u)
		return -EINVAL;
	readcount += r;

	r = parse_package(dmc, data + readcount, size - readcount);
	if (r == 0u)
		return -EINVAL;
	readcount += r;

	for (id = PARITY_DMC_FW_MAIN; id < PARITY_DMC_FW_MAX; id++) {
		if (!dmc->dmc_info[id].present)
			continue;
		offset = readcount + dmc->dmc_info[id].dmc_offset * 4u;   /* dmc_offset dwords */
		if (offset > size) {
			kern_logf("i915: parity dmc: reading beyond fw_size (id %d)\n", id);
			continue;
		}
		(void)parse_header(dmc, data + offset, size - offset, id);
	}

	return parity_dmc_has_payload(dmc) ? 0 : -ENOENT;
}

void
parity_dmc_parse_reset(struct parity_dmc *dmc)
{
	unsigned i;

	for (i = 0u; i < PARITY_DMC_FW_MAX; i++) {
		dmc->dmc_info[i].payload = 0;
		dmc->dmc_info[i].payload_size = 0u;
		dmc->dmc_info[i].present = 0;
	}
	g_dmc_arena_used = 0u;   /* release the device-owned payload storage */
}

int
parity_dmc_has_payload(const struct parity_dmc *dmc)
{
	return dmc->dmc_info[PARITY_DMC_FW_MAIN].payload != 0;
}

/* --- F2: intel_dmc_load_program (see dmc.h) --- */

/* Event-register bases (intel_dmc_regs.h): MAIN 0x8f000; pipe DMCs 0x5f000 + 0x400*(id-1). */
#define DMC_MAIN_REG_BASE     0x8f000u
#define ADLP_PIPEDMC_BASE_A   0x5f000u
#define DMC_EVT_HTP_0         0x8f004u
#define DMC_EVT_CTL_0         0x8f034u
#define DMC_EVENT_HANDLER_COUNT 8u
/* disable value: TYPE_EDGE_0_1 (3<<16) | EVENT_ID_FALSE (0x01<<8). */
#define DMC_EVT_DISABLE_CTL   ((3u << 16) | (0x01u << 8))
#define CLKGATE_DIS_PSL_EXT_A 0x4654Cu
#define PIPEDMC_GATING_DIS    (1u << 12)
#define DC_STATE_DEBUG_REG    0x45520u
#define DC_STATE_DEBUG_MASK   0x3u     /* MASK_CORES | MASK_MEMORY_UP */

static uint32_t
dmc_reg_base(int id)
{
	return (id == PARITY_DMC_FW_MAIN) ? DMC_MAIN_REG_BASE
		: (ADLP_PIPEDMC_BASE_A + 0x400u * (uint32_t)(id - 1));
}
/* _DMC_REG(id, reg) = reg - MAIN_BASE + base(id) (rebased from the MAIN-relative def). */
static uint32_t dmc_reg(int id, uint32_t reg) { return reg - DMC_MAIN_REG_BASE + dmc_reg_base(id); }
static uint32_t dmc_evt_ctl(int id, unsigned h) { return dmc_reg(id, DMC_EVT_CTL_0) + 4u * h; }
static uint32_t dmc_evt_htp(int id, unsigned h) { return dmc_reg(id, DMC_EVT_HTP_0) + 4u * h; }

static int
is_evt_ctl(int id, uint32_t addr)
{
	uint32_t s = dmc_evt_ctl(id, 0u);
	uint32_t e = dmc_evt_ctl(id, DMC_EVENT_HANDLER_COUNT);

	return (addr >= s && addr < e) ? 1 : 0;
}

/* dmc_mmiodata(): ADL-P keeps all pipe-DMC EVT_CTL events disabled by default;
 * MAIN entries are written with their raw value (no TGL/ADLS special cases). */
static uint32_t
dmc_mmiodata(struct parity_dmc *dmc, int id, unsigned i)
{
	if (id != PARITY_DMC_FW_MAIN && is_evt_ctl(id, dmc->dmc_info[id].mmioaddr[i]))
		return DMC_EVT_DISABLE_CTL;
	return dmc->dmc_info[id].mmiodata[i];
}

static uint32_t
payload_dword(const uint8_t *p, uint32_t i)
{
	const uint8_t *q = p + 4u * i;

	return (uint32_t)q[0] | ((uint32_t)q[1] << 8) |
	       ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
}

static void
rmw32(struct osdep_mmio *m, uint32_t reg, uint32_t clr, uint32_t set)
{
	uint32_t v = osdep_mmio_raw_read32(m, reg);

	osdep_mmio_raw_write32(m, reg, (v & ~clr) | set);
}

void
parity_intel_dmc_load_program(struct parity_dmc *dmc, struct osdep_mmio *m,
	uint32_t *dc_state_out)
{
	int id;
	unsigned h, i;
	uint32_t pipe;

	dmc->payload_writes = 0u;
	dmc->aux_writes = 0u;
	dmc->evt_disable_writes = 0u;
	dmc->load_seq_completed = 0;
	dmc->psum = 0u;
	dmc->asum = 0u;

	if (!parity_dmc_has_payload(dmc))
		return;

	/* Wa_16015201720:adl-p pre: pipes A..D. */
	for (pipe = 0u; pipe <= 3u; pipe++)
		rmw32(m, CLKGATE_DIS_PSL_EXT_A + pipe * 4u, 0u, PIPEDMC_GATING_DIS);

	/* disable_all_event_handlers (DISPLAY_VER >= 12): CTL(=FALSE)/HTP(=0). */
	for (id = PARITY_DMC_FW_MAIN; id < PARITY_DMC_FW_MAX; id++) {
		if (dmc->dmc_info[id].payload == 0)
			continue;
		for (h = 0u; h < DMC_EVENT_HANDLER_COUNT; h++) {
			osdep_mmio_raw_write32(m, dmc_evt_ctl(id, h), DMC_EVT_DISABLE_CTL);
			osdep_mmio_raw_write32(m, dmc_evt_htp(id, h), 0u);
			dmc->evt_disable_writes += 2u;
		}
	}

	/* payload writes: preempt-disabled, write_fw (unlocked). */
	kern_preempt_disable();
	for (id = PARITY_DMC_FW_MAIN; id < PARITY_DMC_FW_MAX; id++) {
		struct parity_dmc_info *info = &dmc->dmc_info[id];

		if (info->payload == 0)
			continue;
		for (i = 0u; i < info->dmc_fw_size; i++) {
			uint32_t addr = info->start_mmioaddr + i * 4u;   /* DMC_PROGRAM(start, i) */
			uint32_t val = payload_dword(info->payload, i);

			osdep_mmio_raw_write32(m, addr, val);
			dmc->psum = dmc->psum * 1000003u + addr + (uint64_t)val * 7u;
			dmc->payload_writes++;
			if (parity_dmc_test_fault_at != 0u &&
			    dmc->payload_writes >= parity_dmc_test_fault_at) {
				/* adaptation-layer fault: restore preemption and stop;
				 * the sequence is NOT completed (no aux / post WA). */
				kern_preempt_enable();
				return;
			}
		}
	}
	kern_preempt_enable();

	/* trailing per-DMC MMIO, transformed via dmc_mmiodata(). */
	for (id = PARITY_DMC_FW_MAIN; id < PARITY_DMC_FW_MAX; id++) {
		struct parity_dmc_info *info = &dmc->dmc_info[id];

		if (info->payload == 0)
			continue;
		for (i = 0u; i < info->mmio_count; i++) {
			uint32_t addr = info->mmioaddr[i];
			uint32_t val = dmc_mmiodata(dmc, id, i);

			osdep_mmio_raw_write32(m, addr, val);
			dmc->asum = dmc->asum * 1000003u + addr + (uint64_t)val * 7u;
			dmc->aux_writes++;
		}
	}

	if (dc_state_out != 0)
		*dc_state_out = 0u;   /* power_domains->dc_state = 0 */

	/* gen9_set_dc_state_debugmask(): rmw + posting read. */
	rmw32(m, DC_STATE_DEBUG_REG, 0u, DC_STATE_DEBUG_MASK);
	(void)osdep_mmio_raw_read32(m, DC_STATE_DEBUG_REG);

	/* Wa_16015201720:adl-p post: pipes C..D only (NOT a symmetric undo of pre). */
	for (pipe = 2u; pipe <= 3u; pipe++)
		rmw32(m, CLKGATE_DIS_PSL_EXT_A + pipe * 4u, PIPEDMC_GATING_DIS, 0u);

	dmc->load_seq_completed = 1;
}

/* --- F3: async lifecycle (init / worker / fini) --- */

#define ADLP_DMC_FALLBACK_PATH "i915/adlp_dmc_ver2_16.bin"

static void
dmc_get_ref(struct parity_dmc_dev *d)
{
	/* DMC's OWN POWER_DOMAIN_INIT reference (distinct from the parent's). */
	(void)parity_display_power_get(d->pd, PARITY_PW_DOMAIN_INIT, d->pwc);
	d->dmc_wakeref_held = 1;
}

static void
dmc_put_ref(struct parity_dmc_dev *d)
{
	if (d->dmc_wakeref_held) {
		parity_display_power_put(d->pd, PARITY_PW_DOMAIN_INIT, d->pwc);
		d->dmc_wakeref_held = 0;
	}
}

/* dmc_load_work_fn(): acquire firmware -> parse -> load -> handle the DMC ref. */
static void
dmc_load_work_fn(void *ctx)
{
	struct parity_dmc_dev *d = (struct parity_dmc_dev *)ctx;
	struct osdep_firmware fw;
	int rc;

	d->worker_started = 1;

	rc = osdep_request_firmware(&fw, d->fw_path);
	/*
	 * dmc_fallback_path(): the fallback firmware exists for Alder Lake-P and for nothing else,
	 * so on any other display there is nothing to fall back to (E-126).
	 */
	if (rc == -ENOENT && d->is_alderlake_p) {
		d->fallback_requested = 1;
		rc = osdep_request_firmware(&fw, ADLP_DMC_FALLBACK_PATH);
		if (rc == 0)
			d->fw_path = ADLP_DMC_FALLBACK_PATH;
	}

	if (rc == 0) {
		d->firmware_acquired = 1;
		(void)parity_parse_dmc_fw(&d->dmc, fw.data, fw.size);
	}

	d->main_payload_present = parity_dmc_has_payload(&d->dmc);

	/* Test hook: park here (yielding) before the preempt-off payload region. */
	if (parity_dmc_test_pause)
		kern_logf("i915: dmc worker: parked (test pause)\n");
	while (parity_dmc_test_pause)
		kern_usleep_range(1000u, 2000u);
	if (d->main_payload_present)
		kern_logf("i915: dmc worker: proceeding to load\n");

	if (d->main_payload_present) {
		parity_intel_dmc_load_program(&d->dmc, d->m, &d->dc_state);
		d->load_seq_completed_flag = d->dmc.load_seq_completed;
		if (d->dmc.load_seq_completed) {
			/* success: release the DMC's own reference. */
			dmc_put_ref(d);
		} else {
			/* adaptation-layer fault mid-load: KEEP the reference. */
			d->first_fault = 1;
		}
	}
	/* no payload (absent/parse fail): KEEP the reference (blocks runtime suspend). */

	if (rc == 0)
		osdep_release_firmware(&fw);   /* provider blob; payload is device-owned */
}

void
parity_intel_dmc_init(struct parity_dmc_dev *d, struct parity_kworkqueue *wq,
	struct osdep_mmio *m, struct parity_power_domains *pd, struct parity_pw_ctx *pwc,
	int display_ver, int is_alderlake_p, char stepping, char substepping, const char *fw_path)
{
	/* Take the DMC's own reference first (held on failure). */
	d->pd = pd;
	d->pwc = pwc;
	d->m = m;
	d->wq = wq;
	d->dmc_wakeref_held = 0;
	dmc_get_ref(d);

	d->is_alderlake_p = is_alderlake_p;
	d->fw_path = (fw_path != 0) ? fw_path : "i915/adlp_dmc.bin";
	d->dc_state = 0xffffffffu;
	d->work_submitted = 0;
	d->worker_started = 0;
	d->firmware_acquired = 0;
	d->fallback_requested = 0;
	d->main_payload_present = 0;
	d->load_seq_completed_flag = 0;
	d->first_fault = 0;
	parity_dmc_prepare(&d->dmc, display_ver, stepping, substepping);

	/*
	 * Publish everything the worker reads BEFORE queueing: the worker may start
	 * on another CPU before this returns.  queue_work() orders the prior writes.
	 */
	parity_kwork_init(&d->work, dmc_load_work_fn, d);
	d->work_submitted = 1;
	(void)parity_kqueue_work(wq, &d->work);
}

void
parity_intel_dmc_fini(struct parity_dmc_dev *d, uint64_t deadline)
{
	/* Wait for the worker to complete (flush, NOT cancel). */
	(void)parity_kflush_work(d->wq, &d->work, deadline);

	/* Process any DMC reference the worker left held (failure / fault). */
	dmc_put_ref(d);

	/* Bulk-release the device-owned payload arena + parse state. */
	parity_dmc_parse_reset(&d->dmc);
}
