/*
 * The PCODE equivalence test: runs drv_i915_pcode_read, _write and
 * _request through scripted mailbox answers and prints every register
 * access, wait, lock, nap and log, so the outputs of two versions of
 * power.c can be compared line by line.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "power.h"

struct mutex { int held; };

/* The script: the command register's run/busy on the first read, the wait results, the replies, the counter steps. */
static uint32_t busy_reads[64];
static unsigned busy_n, busy_i;
static int wait_err[64];
static uint32_t wait_last[64];
static unsigned wait_n, wait_i;
static uint32_t data0_reply, data1_reply;
static uint64_t counter, step;
static int counter_fail_at;
static unsigned counter_calls;

uint32_t
drv_i915_raw_read32(struct i915_mmio *mmio, uint32_t offset)
{
	uint32_t v;

	(void)mmio;
	if (offset == 0x138124U)
		v = busy_i < busy_n ? busy_reads[busy_i++] : 0U;
	else if (offset == 0x138128U)
		v = data0_reply++;
	else
		v = data1_reply;
	printf("r %06x -> %08x\n", offset, v);
	return v;
}

void
drv_i915_raw_write32(struct i915_mmio *mmio, uint32_t offset, uint32_t value)
{
	(void)mmio;
	printf("w %06x <- %08x\n", offset, value);
}

int
drv_i915_wait_reg(struct i915_mmio *mmio, uint32_t reg, uint32_t mask, uint32_t value, unsigned fast_us, unsigned slow_ms, uint32_t *last)
{
	int e;

	(void)mmio;
	e = wait_i < wait_n ? wait_err[wait_i] : 0;
	*last = wait_i < wait_n ? wait_last[wait_i] : 0U;
	wait_i++;
	printf("wait %06x %08x %08x %u %u -> %d %08x\n", reg, mask, value, fast_us, slow_ms, e, *last);
	return e;
}

bool
kern_rtc_read_counter(uint64_t *c, uint64_t *hz)
{
	counter_calls++;
	if (counter_fail_at != 0 && (int)counter_calls == counter_fail_at) {
		printf("rtc fail\n");
		return false;
	}
	*c = counter;
	*hz = 1000000U;
	counter += step;
	return true;
}

void kern_usleep_range(unsigned a, unsigned b) { printf("nap %u %u\n", a, b); }
void kern_compiler_barrier(void) { }
void kern_preempt_disable(void) { printf("preempt off\n"); }
void kern_preempt_enable(void) { printf("preempt on\n"); }
void mutex_lock(struct mutex *m) { m->held = 1; printf("lock\n"); }
void mutex_unlock(struct mutex *m) { m->held = 0; printf("unlock\n"); }

void
pcode_equiv_log(const char *fmt, ...)
{
	va_list ap;

	printf("log ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void
reset(void)
{
	memset(busy_reads, 0, sizeof(busy_reads));
	memset(wait_err, 0, sizeof(wait_err));
	memset(wait_last, 0, sizeof(wait_last));
	busy_n = busy_i = wait_n = wait_i = 0;
	data0_reply = 0x100U;
	data1_reply = 0x200U;
	counter = 0;
	step = 1000;
	counter_fail_at = 0;
	counter_calls = 0;
}

int
main(void)
{
	struct mutex lock = { 0 };
	uint32_t v, v1;
	int e;
	unsigned i;

	/* 1 read without the second word. */
	reset(); v = 7; e = drv_i915_pcode_read(&lock, NULL, 0x17U, &v, NULL); printf("= %d %x\n", e, v);
	/* 2 read with the second word. */
	reset(); v = 7; v1 = 9; e = drv_i915_pcode_read(&lock, NULL, 0x17U, &v, &v1); printf("= %d %x %x\n", e, v, v1);
	/* 3 a busy mailbox. */
	reset(); busy_reads[0] = 0x80000000U; busy_n = 1; v = 7; v1 = 9; e = drv_i915_pcode_read(&lock, NULL, 0x17U, &v, &v1); printf("= %d %x %x\n", e, v, v1);
	/* 4 a wait that times out. */
	reset(); wait_err[0] = 110; wait_n = 1; v = 7; v1 = 9; e = drv_i915_pcode_read(&lock, NULL, 0x17U, &v, &v1); printf("= %d %x %x\n", e, v, v1);
	/* 5 each status code on a read. */
	for (i = 0; i < 0x13U; i++) {
		reset(); wait_last[0] = i; wait_n = 1; v = 7; v1 = 9; e = drv_i915_pcode_read(&lock, NULL, 0x17U, &v, &v1); printf("= %d %x %x\n", e, v, v1);
	}
	/* 6 writes: ok, rejected, busy. */
	reset(); e = drv_i915_pcode_write(&lock, NULL, 0x7U, 0x55U); printf("= %d\n", e);
	reset(); wait_last[0] = 0x11U; wait_n = 1; e = drv_i915_pcode_write(&lock, NULL, 0x7U, 0x55U); printf("= %d\n", e);
	reset(); busy_reads[0] = 0x80000000U; busy_n = 1; e = drv_i915_pcode_write(&lock, NULL, 0x7U, 0x55U); printf("= %d\n", e);
	/* 7 a request whose first reply matches (data0 0x101 & 1). */
	reset(); data0_reply = 0x101U; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0x1U, 0x1U, 5); printf("= %d\n", e);
	/* 8 a request that matches on the third ask. */
	reset(); data0_reply = 0x0feU; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0x3U, 0x0U, 5); printf("= %d\n", e);
	/* 9 a request that never matches: base timeout, then the last stretch. */
	reset(); data0_reply = 0x0U; step = 2000; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0xffffffffU, 0xdeadU, 5); printf("= %d\n", e);
	/* 10 a request with a status code on every ask. */
	reset(); for (i = 0; i < 64; i++) wait_last[i] = 0x06U; wait_n = 64; step = 3000; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0x1U, 0x1U, 5); printf("= %d\n", e);
	/* 11 a request whose counter fails at the start, and on the second read. */
	reset(); counter_fail_at = 1; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0x1U, 0x1U, 5); printf("= %d\n", e);
	reset(); counter_fail_at = 2; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0x1U, 0x1U, 5); printf("= %d\n", e);
	/* 12 a busy mailbox on the first ask, then a match. */
	reset(); busy_reads[0] = 0x80000000U; busy_n = 1; data0_reply = 0x101U; e = drv_i915_pcode_request(&lock, NULL, 0x7U, 0x3U, 0x1U, 0x1U, 5); printf("= %d\n", e);
	return 0;
}
