/*
 * The context image equivalence test: builds context images for every
 * engine class of Gen12 and Gen9, with and without a default state, after
 * a scrub and with a non-empty ring, and prints a digest of every page and
 * the context fields, so two versions of context.c can be compared.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "device-info.h"
#include "engine.h"
#include "memory.h"
#include "ppgtt.h"

#ifdef CONTEXT_EQUIV_OLD
#define BUILD drv_i915_lrc_init_state
#define SET_RING drv_i915_lrc_update_regs
#define SCRUB drv_i915_lrc_reset
#define REGS lrc_reg_state
#define DESC lrc_desc
#else
#define BUILD drv_i915_lrc_build_image
#define SET_RING drv_i915_lrc_set_ring
#define SCRUB drv_i915_lrc_scrub
#define REGS regs
#define DESC desc
#endif

static struct i915_gt_object objs[64];
static unsigned nobj, fail_create, fail_bind, ncreate, nbind;
static uint64_t next_ggtt;

void *kern_memset(void *d, int c, size_t n) { return memset(d, c, n); }
void *kern_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
struct i915_gt_object *
drv_i915_gt_object_create(struct i915_gt_mem *gm, uint32_t bytes)
{
	struct i915_gt_object *o;

	(void)gm;
	ncreate++;
	printf("create %u\n", bytes);
	if (ncreate == fail_create)
		return NULL;
	o = &objs[nobj++];
	memset(o, 0, sizeof(*o));
	o->bytes = (bytes + 4095U) & ~4095U;
	o->cpu = aligned_alloc(4096, o->bytes);
	memset(o->cpu, 0x5a, o->bytes);
	return o;
}
void drv_i915_gt_object_destroy(struct i915_gt_mem *gm, struct i915_gt_object *o) { (void)gm; printf("destroy %u\n", (unsigned)(o - objs)); }
int drv_i915_gt_ggtt_bind(struct i915_gt_mem *gm, struct i915_gt_object *o)
{
	(void)gm;
	nbind++;
	printf("bind %u\n", (unsigned)(o - objs));
	if (nbind == fail_bind)
		return 5;
	o->ggtt_offset = next_ggtt;
	next_ggtt += o->bytes;
	return 0;
}
uint32_t *drv_i915_gen12_emit_aux_table_inv(int id, uint32_t *cs) { *cs++ = 0xa0000000U | (uint32_t)id; *cs++ = 0xa1; *cs++ = 0xa2; return cs; }

static uint64_t
fnv(const void *p, size_t n)
{
	const unsigned char *b = p;
	uint64_t h = 1469598103934665603ULL;
	size_t i;

	for (i = 0; i < n; i++)
		h = (h ^ b[i]) * 1099511628211ULL;
	return h;
}

static void
dump(const char *what, struct i915_gt_context *ce)
{
	unsigned p;

	printf("%s alloc=%d bytes=%u wa=%u ring=%u/%u/%u/%u/%u ggtt=%llx lrca=%08x desc=%016llx tag=%d dw=%u ind=%08x/%u per=%d", what, ce->allocated, ce->state_bytes, ce->wa_bb_page,
	    ce->ring.size, ce->ring.head, ce->ring.tail, ce->ring.emit, ce->ring.space, (unsigned long long)ce->ring.ggtt_offset, ce->lrca, (unsigned long long)ce->DESC, ce->tag, ce->reg_state_dwords, ce->indirect_bb_ggtt, ce->indirect_bb_dwords, ce->per_ctx_bb_set);
	if (ce->state != NULL) {
		printf(" regsoff=%ld", (long)((char *)ce->REGS - (char *)ce->state->cpu));
		for (p = 0; p < ce->state_bytes / 4096U; p++)
			printf(" p%u=%016llx", p, (unsigned long long)fnv((char *)ce->state->cpu + p * 4096U, 4096));
	}
	printf("\n");
}

static void
dump_page(struct i915_gt_context *ce, unsigned page)
{
	const uint32_t *w = (const uint32_t *)((char *)ce->state->cpu + page * 4096U);
	unsigned i;

	for (i = 0; i < 1024; i++)
		if (w[i] != 0x5a5a5a5aU)
			printf(" [%u:%03x]=%08x", page, i, w[i]);
	printf("\n");
}

int
main(void)
{
	static struct i915_engine_info infos[10];
	static struct i915_gt_engine ges[10];
	static struct i915_gt_ppgtt vm;
	static struct i915_gt_context ce;
	static struct i915_gt_object def;
	struct i915_gt_mem gm;
	static const int classes[5] = { I915_RENDER_CLASS, I915_COPY_ENGINE_CLASS, I915_VIDEO_DECODE_CLASS, I915_VIDEO_DECODE_CLASS, I915_VIDEO_ENHANCEMENT_CLASS };
	static const int ids[5] = { I915_RCS0, I915_BCS0, I915_VCS0, I915_VCS2, I915_VECS0 };
	static const uint32_t bases[5] = { 0x2000, 0x22000, 0x1c0000, 0x1d0000, 0x1c8000 };
	unsigned g, e, mode, page;

	memset(&gm, 0, sizeof(gm));
	vm.top_pd_dma = 0x123456789000ULL;
	def.cpu = malloc(0x20000);
	memset(def.cpu, 0x33, 0x20000);
	def.bytes = 0x20000;
	for (g = 0; g < 2; g++) {
		for (e = 0; e < 5; e++) {
			struct i915_engine_info *in = &infos[g * 5 + e];
			struct i915_gt_engine *ge = &ges[g * 5 + e];

			in->class = classes[e];
			in->id = ids[e];
			in->mmio_base = bases[e];
			in->graphics_ver = g == 0 ? 12 : 9;
			in->context_size = classes[e] == I915_RENDER_CLASS ? 0x13000U + 0x123U * g : 0x2000U + 0x10U * e;
			ge->info = in;
			ge->sseu_slice_mask = g == 0 ? 0x1 : 0x3;
			ge->sseu_has_slice_pg = (int)(e & 1U) ^ (int)g;
			ge->sseu_max_eus_per_subslice = 8;
			ge->sseu_eu_per_subslice = (uint16_t)(g == 0 ? 2 : 8);
			for (mode = 0; mode < 3; mode++) {
				printf("== gen %u engine %u mode %u\n", in->graphics_ver, e, mode);
				ge->default_state = mode == 1 ? &def : NULL;
				nobj = 0; next_ggtt = 0x100000;
				printf("= %d\n", drv_i915_lrc_alloc(&ce, ge, &vm, &gm, mode == 2 ? 0x8000 : 0x4000, 7));
				BUILD(&ce);
				dump("built", &ce);
				ce.ring.emit = 0x140; ce.ring.tail = 0x140;
				printf("set %08x\n", SET_RING(&ce, 0x40));
				dump("set", &ce);
				for (page = 1; page < ce.state_bytes / 4096U; page++)
					if (page == 1 || page >= ce.wa_bb_page) { printf("page %u:", page); dump_page(&ce, page); }
				ce.DESC |= 0xabcd000000000000ULL;
				if (mode == 2) { SCRUB(&ce); dump("scrub", &ce); printf("page 1:"); dump_page(&ce, 1); }
				printf("set2 %08x\n", SET_RING(&ce, 0x80));
				dump("set2", &ce);
				drv_i915_lrc_release(&ce, &gm);
				dump("released", &ce);
			}
		}
	}
	/* failures and invalid arguments */
	for (e = 1; e <= 4; e++) {
		ncreate = nbind = 0; fail_create = e <= 2 ? e : 0; fail_bind = e > 2 ? e - 2 : 0;
		printf("fail %u = %d\n", e, drv_i915_lrc_alloc(&ce, &ges[0], &vm, &gm, 0x4000, 1));
		dump("failed", &ce);
	}
	ncreate = nbind = 0; fail_create = fail_bind = 0;
	printf("npot = %d\n", drv_i915_lrc_alloc(&ce, &ges[0], &vm, &gm, 0x3000, 1));
	printf("null = %d %d\n", drv_i915_lrc_alloc(&ce, NULL, &vm, &gm, 0x4000, 1), drv_i915_lrc_alloc(&ce, &ges[0], &vm, &gm, 0, 1));
	ce.allocated = 0; BUILD(&ce); SCRUB(&ce); printf("unalloc set %08x\n", SET_RING(&ce, 0));
	printf("aux %08x %08x %08x %08x %08x %08x\n", drv_i915_lrc_aux_inv_reg(I915_RCS0), drv_i915_lrc_aux_inv_reg(I915_BCS0), drv_i915_lrc_aux_inv_reg(I915_VCS0), drv_i915_lrc_aux_inv_reg(I915_VCS2), drv_i915_lrc_aux_inv_reg(I915_VECS0), drv_i915_lrc_aux_inv_reg(99));
	return 0;
}
