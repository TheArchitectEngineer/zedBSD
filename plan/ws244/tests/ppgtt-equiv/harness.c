/*
 * The PPGTT equivalence test: builds kernel and session address spaces over
 * deterministic fake pages and prints every table creation, flush, entry
 * and result, so two versions of ppgtt.c can be compared line by line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "ppgtt.h"

#ifdef PPGTT_EQUIV_OLD
#define LEAF_ENCODE drv_i915_gen12_ppgtt_pte_encode
#define DIR_UNCACHED drv_i915_gen8_pde_encode
#define DIR_CACHED drv_i915_gen8_pde_encode_cached
#else
#define LEAF_ENCODE drv_i915_ppgtt_leaf_encode
#define DIR_UNCACHED drv_i915_ppgtt_dir_encode_uncached
#define DIR_CACHED drv_i915_ppgtt_dir_encode
#endif

#define POOL 256
static uint64_t pool[POOL][512];
static unsigned pool_used, objects, create_fail_at, pmem_fail_at, pmem_n;
static struct i915_gt_object objs[POOL];
static uint64_t dma_bad;

static unsigned id_of(const volatile void *p) { return (unsigned)(((const uint64_t *)p - &pool[0][0]) / 512); }

struct i915_gt_object *
drv_i915_gt_object_create(struct i915_gt_mem *gm, uint32_t bytes)
{
	struct i915_gt_object *o;

	(void)gm;
	objects++;
	if (objects == create_fail_at) { printf("create %u fail\n", bytes); return NULL; }
	o = &objs[pool_used];
	o->id = pool_used;
	o->cpu = pool[pool_used++];
	memset(o->cpu, 0xa5, 4096);
	printf("create %u -> %u\n", bytes, o->id);
	return o;
}
void drv_i915_gt_object_destroy(struct i915_gt_mem *gm, struct i915_gt_object *o) { (void)gm; printf("destroy %u\n", o->id); }
int drv_i915_gt_object_page_dma(const struct i915_gt_object *o, unsigned page, uint64_t *d) { *d = 0x100000ULL + 0x1000ULL * o->id; printf("dma %u %u\n", o->id, page); return 0; }
void drv_i915_gt_clflush(const volatile void *a, size_t n)
{
	const uint64_t *e = (const uint64_t *)a;
	unsigned id = id_of(a);
	printf("flush %u+%u %zu", id, (unsigned)(e - pool[id]), n);
	if (n == 8)
		printf(" = %016llx", (unsigned long long)*e);
	else
		printf(" [0]=%016llx [511]=%016llx", (unsigned long long)e[0], (unsigned long long)e[511]);
	printf("\n");
}
int drv_i915_dma_in_range(uint64_t d, uint64_t l, uint64_t m) { (void)l; (void)m; return d != dma_bad; }
void kern_io_write_barrier(void) { printf("barrier\n"); }
void *kern_calloc(size_t n, size_t s) { return calloc(n, s); }
void kern_free(void *p) { free(p); }
int kern_pmem_alloc_limited(uint64_t size, uint64_t align, uint64_t limit, unsigned flags, struct kern_pmem *run)
{
	pmem_n++;
	printf("pmem_alloc %llu %llu %llx %u", (unsigned long long)size, (unsigned long long)align, (unsigned long long)limit, flags);
	if (pmem_n == pmem_fail_at) { printf(" fail\n"); return 12; }
	run->paddr = 0x100000ULL + 0x1000ULL * pool_used++;
	run->size = size;
	printf(" -> %llx\n", (unsigned long long)run->paddr);
	return 0;
}
int kern_pmem_free(struct kern_pmem *run) { printf("pmem_free %llx\n", (unsigned long long)run->paddr); return 0; }
void *kern_pmem_to_kernel(hal_physaddr_t p) { return pool[(p - 0x100000ULL) / 0x1000ULL]; }

static void
fn(struct i915_gt_ppgtt *pp, struct i915_gt_object *pt, uint64_t dma, void *data)
{
	(void)pp;
	printf("pt %u %llx %s\n", pt->id, (unsigned long long)dma, data == (void *)fn ? "d" : "?");
}

static void
dump(struct i915_gt_ppgtt *pp)
{
	unsigned i;

	printf("pp inited=%d top=%d count=%u n=%u topdma=%llx", pp->inited, pp->top, pp->top_count, pp->n_tables, (unsigned long long)pp->top_pd_dma);
	for (i = 0; i < 4; i++)
		printf(" s%u=%016llx", i, (unsigned long long)pp->scratch_encode[i]);
	for (i = 0; i < pp->n_tables; i++)
		printf(" [%u p%u i%u l%d %llx]", pp->tables[i].obj->id, pp->tables[i].parent->id, pp->tables[i].idx, pp->tables[i].lvl, (unsigned long long)pp->tables[i].dma);
	printf("\n");
}

static void
fresh(void)
{
	pool_used = objects = pmem_n = 0;
	create_fail_at = pmem_fail_at = 0;
	dma_bad = ~0ULL;
}

int
main(void)
{
	static struct i915_gt_ppgtt pp;
	static struct i915_ppgtt vm;
	struct i915_gt_mem gm = { ~0ULL };
	uint64_t va;
	unsigned i;
	static const uint64_t ranges[][2] = {
		{ 0, 0x1000 }, { 0x1ff000, 0x2000 }, { 0x3ff000, 0x401000 }, { 0x3fffff000ULL, 0x2000 },
		{ 0x7ffffff000ULL, 0x2000 }, { 0x200000, 0x200000 }, { 0x1000, 0xa00000 }, { 0xfffffffff000ULL, 0x1000 },
	};

	printf("encode %016llx %016llx %016llx\n", (unsigned long long)LEAF_ENCODE(0x5000, 7), (unsigned long long)DIR_UNCACHED(0x6000), (unsigned long long)DIR_CACHED(0x7000));

	/* create failures: each object, and an out-of-mask page */
	for (i = 1; i <= 6; i++) { fresh(); create_fail_at = i; printf("= %d\n", drv_i915_gt_ppgtt_create(&gm, &pp)); dump(&pp); }
	fresh(); dma_bad = 0x102000; printf("= %d\n", drv_i915_gt_ppgtt_create(&gm, &pp)); dump(&pp);
	fresh(); dma_bad = 0x104000; printf("= %d\n", drv_i915_gt_ppgtt_create(&gm, &pp)); dump(&pp);

	/* each range in a fresh space: alloc, foreach, insert, scratch */
	for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
		fresh(); printf("== range %llx %llx\n", (unsigned long long)ranges[i][0], (unsigned long long)ranges[i][1]);
		printf("= %d\n", drv_i915_gt_ppgtt_create(&gm, &pp));
		printf("foreach before = %d\n", drv_i915_gt_ppgtt_foreach_pt(&pp, ranges[i][0], ranges[i][1], fn, (void *)fn));
		printf("insert before = %d\n", drv_i915_gt_ppgtt_insert_page(&pp, 0x9000, ranges[i][0], 3));
		printf("alloc = %d\n", drv_i915_gt_ppgtt_alloc_range(&gm, &pp, ranges[i][0], ranges[i][1]));
		dump(&pp);
		printf("alloc again = %d\n", drv_i915_gt_ppgtt_alloc_range(&gm, &pp, ranges[i][0], ranges[i][1]));
		printf("foreach = %d\n", drv_i915_gt_ppgtt_foreach_pt(&pp, ranges[i][0], ranges[i][1], fn, (void *)fn));
		printf("foreach wider = %d\n", drv_i915_gt_ppgtt_foreach_pt(&pp, ranges[i][0], ranges[i][1] + 0x200000, fn, (void *)fn));
		printf("insert = %d\n", drv_i915_gt_ppgtt_insert_page(&pp, 0x9000, ranges[i][0] + ranges[i][1] - 0x1000, 3));
		printf("scratch = %d\n", drv_i915_gt_ppgtt_insert_scratch(&pp, ranges[i][0]));
		printf("bad = %d %d %d\n", drv_i915_gt_ppgtt_alloc_range(&gm, &pp, 1, 0x1000), drv_i915_gt_ppgtt_alloc_range(&gm, &pp, 0xfffffffff000ULL, 0x2000), drv_i915_gt_ppgtt_foreach_pt(&pp, 0, 0, fn, NULL));
		drv_i915_gt_ppgtt_destroy(&gm, &pp); dump(&pp);
	}
	/* the table list fills, and a table creation and DMA failure */
	fresh(); (void)drv_i915_gt_ppgtt_create(&gm, &pp); printf("full = %d\n", drv_i915_gt_ppgtt_alloc_range(&gm, &pp, 0, 0x2000000)); dump(&pp); drv_i915_gt_ppgtt_destroy(&gm, &pp);
	fresh(); (void)drv_i915_gt_ppgtt_create(&gm, &pp); create_fail_at = objects + 3; printf("fail = %d\n", drv_i915_gt_ppgtt_alloc_range(&gm, &pp, 0, 0x400000)); dump(&pp); drv_i915_gt_ppgtt_destroy(&gm, &pp);
	fresh(); (void)drv_i915_gt_ppgtt_create(&gm, &pp); dma_bad = 0x100000ULL + 0x1000ULL * (pool_used + 1); printf("range = %d\n", drv_i915_gt_ppgtt_alloc_range(&gm, &pp, 0, 0x400000)); dump(&pp); drv_i915_gt_ppgtt_destroy(&gm, &pp);

	/* the session space */
	fresh(); memset(&vm, 0, sizeof(vm)); printf("s create = %d\n", drv_i915_ppgtt_create(&vm));
	printf("s again = %d\n", drv_i915_ppgtt_create(&vm));
	printf("s va = %d", drv_i915_ppgtt_va_alloc(&vm, 0x3000, &va)); printf(" %llx\n", (unsigned long long)va);
	printf("s insert = %d\n", drv_i915_ppgtt_insert(&vm, va, 0x40000000, 3));
	printf("s uc = %d\n", drv_i915_ppgtt_insert_uncached(&vm, va + 0x200000, 0x50000000, 2));
	printf("s leaf %016llx\n", (unsigned long long)pool[pool_used - 1][(va >> 12) & 511]);
	drv_i915_ppgtt_clear(&vm, va, 3);
	printf("s pages %u\n", vm.page_count);
	drv_i915_ppgtt_destroy(&vm);
	fresh(); memset(&vm, 0, sizeof(vm)); pmem_fail_at = 3; printf("s fail = %d\n", drv_i915_ppgtt_create(&vm));
	return 0;
}
