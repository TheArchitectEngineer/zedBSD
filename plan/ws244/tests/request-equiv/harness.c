/*
 * The request equivalence test: writes requests for every engine class of
 * Gen12 and Gen9 (open with each flush mode, context workarounds, close)
 * and the ring refusals, and prints every ring dword and request field, so
 * two versions of request.c can be compared line by line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "device-info.h"
#include "engine.h"
#include "mmio.h"
#include "request.h"
#include "workarounds.h"

#include "intel/commands.h"

#ifdef REQUEST_EQUIV_OLD
#define OPEN drv_i915_request_create
#define CLOSE drv_i915_request_add
#define RESERVE drv_i915_ring_begin
#define COMMIT drv_i915_ring_advance
#define AUX drv_i915_gen12_emit_aux_table_inv
#define CTXWA drv_i915_emit_ctx_wa
#else
#define OPEN drv_i915_request_open
#define CLOSE drv_i915_request_close
#define RESERVE drv_i915_ring_reserve
#define COMMIT drv_i915_ring_commit
#define AUX drv_i915_emit_aux_inv
#define CTXWA drv_i915_emit_context_wa
#endif

void *kern_memset(void *d, int c, size_t n) { return memset(d, c, n); }
uint32_t drv_i915_read32(struct i915_mmio *m, uint32_t reg) { (void)m; printf("read %06x\n", reg); return 0x0f0f0f0fU ^ reg; }
uint32_t
drv_i915_lrc_aux_inv_reg(int id)
{
	switch (id) {
	case I915_RCS0: return 0x4208;
	case I915_BCS0: return 0x4248;
	case I915_VCS0: return 0x4218;
	case I915_VCS2: return 0x4298;
	case I915_VECS0: return 0x4238;
	default: return 0;
	}
}

static uint32_t ring_mem[4096];

static void
show(const char *what, int rc, struct i915_gt_request *rq, struct i915_gt_context *ce, uint32_t from)
{
	uint32_t i;

	printf("%s rc=%d err=%d emit=%u space=%u head=%u tail=%u wa_tail=%u added=%d preempt=%08x:", what, rc, rq->error, ce->ring.emit, ce->ring.space, rq->head, rq->tail, rq->wa_tail, rq->added, rq->preempt_ggtt);
	for (i = from / 4U; i < ce->ring.emit / 4U; i++)
		printf(" %08x", ring_mem[i]);
	printf("\n");
}

static void
fresh(struct i915_gt_context *ce, struct i915_gt_engine *ge, uint32_t size)
{
	memset(ce, 0, sizeof(*ce));
	memset(ring_mem, 0xee, sizeof(ring_mem));
	ce->ge = ge;
	ce->allocated = 1;
	ce->ring.vaddr = ring_mem;
	ce->ring.size = size;
	ce->ring.space = size - 8U;
}

int
main(void)
{
	static struct i915_engine_info infos[10];
	static struct i915_gt_engine ges[10];
	static struct i915_gt_context ce;
	static struct i915_gt_request rq;
	static struct i915_wa_list wal, empty;
	static const int classes[5] = { I915_RENDER_CLASS, I915_COPY_ENGINE_CLASS, I915_VIDEO_DECODE_CLASS, I915_VIDEO_DECODE_CLASS, I915_VIDEO_ENHANCEMENT_CLASS };
	static const int ids[5] = { I915_RCS0, I915_BCS0, I915_VCS0, I915_VCS2, I915_VECS0 };
	struct i915_mmio *mmio = (struct i915_mmio *)&wal;
	volatile uint32_t slot = 0;
	unsigned g, e, m;
	uint32_t *cs;
	int rc;
	static const uint32_t modes[4] = { I915_CMD_EMIT_INVALIDATE, I915_CMD_EMIT_FLUSH, I915_CMD_EMIT_BARRIER, 0 };

	wal.count = 3;
	wal.list[0] = (struct i915_wa){ .reg = 0x7004, .clr = 0xffff0000U, .set = 0x00010001U, .kind = I915_WA_MASKED };
	wal.list[1] = (struct i915_wa){ .reg = 0xe194, .clr = 0xffffffffU, .set = 0x00000040U };
	wal.list[2] = (struct i915_wa){ .reg = 0xb118, .clr = 0x00000f00U, .set = 0x00000300U };
	for (g = 0; g < 2; g++) {
		for (e = 0; e < 5; e++) {
			struct i915_engine_info *in = &infos[g * 5 + e];
			struct i915_gt_engine *ge = &ges[g * 5 + e];

			in->class = classes[e];
			in->id = ids[e];
			in->graphics_ver = g == 0 ? 12 : 9;
			ge->info = in;
			ge->hwsp_ggtt = 0x40000;
			for (m = 0; m < 4; m++) {
				printf("== gen %d engine %u mode %u\n", in->graphics_ver, e, m);
				fresh(&ce, ge, 0x4000);
				rc = OPEN(&rq, &ce, 0x1234 + m, 0x40100, &slot);
				show("open", rc, &rq, &ce, 0);
				/* the flush of each mode through a workaround list of one barrier pair */
				rc = CTXWA(&rq, m == 0 ? &wal : &empty, mmio);
				show("wa", rc, &rq, &ce, 0);
				rc = CLOSE(&rq);
				show("close", rc, &rq, &ce, 0);
				slot = 0x1233 + m; printf("done %d", drv_i915_request_completed(&rq));
				slot = 0x1234 + m; printf(" %d", drv_i915_request_completed(&rq));
				slot = 0x80000000U + 0x1234 + m; printf(" %d\n", drv_i915_request_completed(&rq));
				(void)modes;
			}
			/* a workaround list without mmio, a bad slot, odd and too large reservations, wrap */
			fresh(&ce, ge, 0x4000);
			rc = OPEN(&rq, &ce, 1, 0x40100, &slot); rc = CTXWA(&rq, &wal, NULL); show("wa-nommio", rc, &rq, &ce, 0);
			printf("close-after-fail %d\n", CLOSE(&rq));
			fresh(&ce, ge, 0x4000);
			printf("badslot %d %d\n", OPEN(&rq, &ce, 1, 0x40104, &slot), OPEN(&rq, &ce, 1, 0x40120, &slot));
			fresh(&ce, ge, 0x4000);
			(void)OPEN(&rq, &ce, 1, 0x40100, &slot);
			cs = RESERVE(&rq, 3); printf("odd %p %d\n", (void *)cs, rq.error);
			fresh(&ce, ge, 0x4000);
			(void)OPEN(&rq, &ce, 1, 0x40100, &slot);
			cs = RESERVE(&rq, 0x1000); printf("big %d %d\n", cs == NULL, rq.error);
			fresh(&ce, ge, 0x4000);
			ce.ring.emit = 0x3fc0; ce.ring.head = 0x100;
			rc = OPEN(&rq, &ce, 1, 0x40100, &slot); show("wrap", rc, &rq, &ce, 0x3fc0);
			fresh(&ce, ge, 0x4000);
			ce.ring.emit = 0x100; ce.ring.head = 0x120;
			rc = OPEN(&rq, &ce, 1, 0x40100, &slot); show("full", rc, &rq, &ce, 0x100);
			fresh(&ce, ge, 0x4000);
			(void)OPEN(&rq, &ce, 1, 0x40100, &slot);
			cs = RESERVE(&rq, 2); cs = AUX(in->id, cs); COMMIT(&rq, cs); show("aux", 0, &rq, &ce, 0);
		}
	}
	printf("null %d %d %d %d\n", OPEN(NULL, &ce, 1, 0, NULL), CLOSE(NULL), CTXWA(NULL, &wal, mmio), drv_i915_request_completed(NULL));
	ce.allocated = 0;
	printf("unalloc %d\n", OPEN(&rq, &ce, 1, 0x40100, &slot));
	return 0;
}
