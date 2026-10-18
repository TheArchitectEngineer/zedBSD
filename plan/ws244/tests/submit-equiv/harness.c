/*
 * The execlists equivalence test: binds, enables, submits and drains
 * scripted CSB entries on Gen12 and Gen9 engines, with the CSB restart,
 * quiesce and abandon, and prints every register access, log and state
 * field, so two versions of submit.c can be compared line by line.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The version under test first, before engine.h pulls in the tree's copy. */
#include "submit.h"
#include "context.h"
#include "device-info.h"
#include "engine.h"
#include "mmio.h"
#include "request.h"
#include "intel/gt-regs.h"

#ifdef SUBMIT_EQUIV_OLD
#define BIND drv_i915_execlists_submission_setup
#define RESTART drv_i915_execlists_reset_csb_pointers
#define QUIESCE drv_i915_execlists_reset_prepare
#define ABANDON drv_i915_execlists_reset_rewind
#define DRAIN drv_i915_execlists_process_csb
#define DESC lrc_desc
#define REGS lrc_reg_state
#else
#define BIND drv_i915_execlists_bind
#define RESTART drv_i915_execlists_csb_restart
#define QUIESCE drv_i915_execlists_quiesce
#define ABANDON drv_i915_execlists_abandon
#define DRAIN drv_i915_execlists_drain_csb
#define DESC desc
#define REGS regs
#endif

static uint32_t mmio_val;

void *kern_memset(void *d, int c, size_t n) { return memset(d, c, n); }
void kern_io_write_barrier(void) { printf("wbar\n"); }
void kern_io_read_barrier(void) { printf("rbar\n"); }
void kern_logf(const char *fmt, ...) { va_list ap; printf("log "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
uint32_t drv_i915_read32(struct i915_mmio *m, uint32_t reg) { (void)m; printf("r %06x -> %08x\n", reg, mmio_val + reg); return mmio_val + reg; }
void drv_i915_write32(struct i915_mmio *m, uint32_t reg, uint32_t v) { (void)m; printf("w %06x <- %08x\n", reg, v); }
void drv_i915_posting_read32(struct i915_mmio *m, uint32_t reg) { (void)m; printf("p %06x\n", reg); }
int drv_i915_udelay(unsigned us) { printf("udelay %u\n", us); return 0; }
int drv_i915_engine_stop_cs(struct i915_gt_engine *ge, struct i915_mmio *m) { (void)m; printf("stop_cs %s\n", ge->info->name); return 7; }
void drv_i915_engine_wait_for_pending_mi_fw(struct i915_gt_engine *ge, struct i915_mmio *m) { (void)m; printf("wait_mi_fw %s\n", ge->info->name); }

static uint32_t hwsp[1024];
static uint32_t regs[1024];

static void
show(const char *what, struct i915_gt_engine *ge, struct i915_execlists *el, struct i915_gt_request *done, struct i915_gt_request *rqs)
{
	unsigned i;

	printf("%s done=%ld sub=%06x ctrl=%06x csbw=%ld size=%u head=%u ccid=%08x writes=%u/%u resumed=%d esr=%08x stop=%d pre=%u", what, done ? (long)(done - rqs) : -1L, ge->submit_reg, ge->ctrl_reg,
	    ge->csb_write ? (long)(ge->csb_write - hwsp) : -1L, ge->csb_size, ge->csb_head, ge->ccid, ge->enable_writes, ge->csb_reset_writes, ge->resumed, ge->esr_at_resume, ge->stop_cs_rc, hwsp[I915_GT_HWS_PREEMPT]);
	printf(" | pend=%ld/%ld infl=%ld/%ld act=%d tag=%016llx serial=%u ev=%u pr=%u co=%u late=%u mm=%u err=%u last=%08x:%08x sub=%u",
	    el->pending[0] ? (long)(el->pending[0] - rqs) : -1L, el->pending[1] ? (long)(el->pending[1] - rqs) : -1L,
	    el->inflight[0] ? (long)(el->inflight[0] - rqs) : -1L, el->inflight[1] ? (long)(el->inflight[1] - rqs) : -1L,
	    el->have_active, (unsigned long long)el->context_tag, el->serial, el->csb_events, el->promotes, el->completes, el->csb_late, el->csb_mmio_fallback, el->csb_errors, el->last_csb_hi, el->last_csb_lo, el->submits);
	for (i = 0; i < 12; i++)
		printf(" c%u=%016llx", i, (unsigned long long)((volatile uint64_t *)&hwsp[I915_GT_HWS_CSB_BUF0_INDEX])[i]);
	printf("\n");
}

static void
csb_put(struct i915_gt_engine *ge, unsigned index, uint64_t value, unsigned write)
{
	ge->csb_status[index] = value;
	*ge->csb_write = write;
}

int
main(void)
{
	static struct i915_engine_info infos[4];
	static struct i915_gt_engine ges[4];
	static struct i915_execlists el;
	static struct i915_gt_context ces[4];
	static struct i915_gt_request rqs[4];
	struct i915_mmio *mmio = (struct i915_mmio *)&el;
	static const char *names[4] = { "rcs0", "bcs0", "rcs0-9", "bcs0-9" };
	unsigned g, i, w;
	uint64_t promote, complete;

	for (g = 0; g < 4; g++) {
		struct i915_engine_info *in = &infos[g];
		struct i915_gt_engine *ge = &ges[g];
		uint32_t sizes[2] = { 0x4000, 0x1000 };

		in->class = (g & 1U) ? I915_COPY_ENGINE_CLASS : I915_RENDER_CLASS;
		in->id = (g & 1U) ? I915_BCS0 : I915_RCS0;
		in->instance = (int)g;
		in->mmio_base = (g & 1U) ? 0x22000 : 0x2000;
		in->graphics_ver = g < 2 ? 12 : 9;
		in->name = names[g];
		ge->info = in;
		printf("=== %s\n", in->name);
		memset(hwsp, 0, sizeof(hwsp));
		ge->hwsp = hwsp;
		ge->hwsp_ggtt = 0x80000;
		ge->setup_done = 0; BIND(ge); show("bind-nosetup", ge, &el, NULL, rqs);
		ge->setup_done = 1; BIND(ge); show("bind", ge, &el, NULL, rqs);
		mmio_val = 0; drv_i915_execlists_enable(ge, mmio); show("enable0", ge, &el, NULL, rqs);
		mmio_val = 1; drv_i915_execlists_enable(ge, mmio); show("enable1", ge, &el, NULL, rqs);
		drv_i915_execlists_init(&el);
		RESTART(ge, mmio); show("restart", ge, &el, NULL, rqs);
		promote = g < 2 ? ((uint64_t)(0x7ffU << 15) << 32) : 0x1U;
		complete = g < 2 ? ((uint64_t)(0x1U << 15) << 32) | 0x10U : 0x10U;
		for (i = 0; i < 3; i++) {
			memset(&ces[i], 0, sizeof(ces[i]));
			ces[i].ge = ge;
			ces[i].ring.size = sizes[i & 1U];
			ces[i].ring.tail = 0x100U * i;
			ces[i].REGS = regs;
			ces[i].DESC = 0x0000000000001119ULL | ((uint64_t)i << 12) | 4U;
			ces[i].tag = -1;
			memset(&rqs[i], 0, sizeof(rqs[i]));
			rqs[i].ce = &ces[i];
			rqs[i].added = 1;
			rqs[i].tail = i == 1 ? 0U : 0x200U + 0x40U * i;
			rqs[i].wa_tail = rqs[i].tail + 8U;
		}
		/* submit, a second submit while busy, the promote and the completion */
		printf("submit %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[0]));
		printf("busy %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[1]));
		show("submitted", ge, &el, DRAIN(ge, &el, mmio), rqs);
		csb_put(ge, 0, promote, 0); show("promote", ge, &el, DRAIN(ge, &el, mmio), rqs);
		csb_put(ge, 1, complete, 1); show("complete", ge, &el, DRAIN(ge, &el, mmio), rqs);
		printf("desc %016llx tail %08x rqtail %u\n", (unsigned long long)ces[0].DESC, regs[I915_GT_CTX_RING_TAIL], rqs[0].tail);
		/* a non-advancing tail forces a restore; promote and complete in one walk; a late and an unwritten entry */
		printf("submit %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[1]));
		ge->csb_status[2] = ~(uint64_t)0; ge->csb_status[3] = ~(uint64_t)0; *ge->csb_write = 3;
		mmio_val = 0x10;
		show("unwritten", ge, &el, DRAIN(ge, &el, mmio), rqs);
		/* errors: completion with nothing active, a pointer beyond the ring, a promotion with nothing pending */
		csb_put(ge, 4, complete, 4); show("err-complete", ge, &el, DRAIN(ge, &el, mmio), rqs);
		*ge->csb_write = 40; show("err-beyond", ge, &el, DRAIN(ge, &el, mmio), rqs);
		*ge->csb_write = 4;
		csb_put(ge, 5, promote, 5); show("err-promote", ge, &el, DRAIN(ge, &el, mmio), rqs);
		/* wrap around the ring */
		for (w = 0; w < 6; w++) {
			printf("submit %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[w & 1U ? 2 : 0]));
			csb_put(ge, (ge->csb_head + 1U) % ge->csb_size, promote, (ge->csb_head + 1U) % ge->csb_size);
			csb_put(ge, (ge->csb_head + 2U) % ge->csb_size, complete, (ge->csb_head + 2U) % ge->csb_size);
			show("wrap", ge, &el, DRAIN(ge, &el, mmio), rqs);
		}
		/* abandon with a request in flight and one pending */
		printf("submit %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[0]));
		csb_put(ge, (ge->csb_head + 1U) % ge->csb_size, promote, (ge->csb_head + 1U) % ge->csb_size);
		show("promote", ge, &el, DRAIN(ge, &el, mmio), rqs);
		el.pending[0] = &rqs[2]; ces[2].tag = 5; el.context_tag &= ~(1ULL << 5);
		QUIESCE(ge, mmio); show("quiesce", ge, &el, NULL, rqs);
		ABANDON(ge, &el, mmio); show("abandon", ge, &el, NULL, rqs);
		el.context_tag = 0; printf("notag %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[0]));
		rqs[0].added = 0; printf("notadded %d\n", drv_i915_execlists_submit(ge, &el, mmio, &rqs[0]));
	}
	return 0;
}
