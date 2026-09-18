/*
 * WS031 Linux-parity — in-kernel sync concurrency tests (see ktest.h).
 *
 * Test cases only.  The completion and workqueue under test are the SHARED
 * kernel sync backend (backend_sync.c) that the normal driver path also uses.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <string.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include <kern/thread.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include <hal/hal.h>
#include "backend_sync.h"
#include "ktest.h"
#include "osdep/mmio.h"
#include "reset.h"
#include "pcode.h"
#include "dram_bw.h"
#include "wait.h"
#include "drm_device.h"
#include "bios.h"
#include "vga.h"
#include "power_domains.h"
#include "combo_phy.h"
#include "cdclk.h"
#include "osdep/firmware.h"
#include "dmc.h"
#include "display_state.h"
#include "pch.h"
#include "backend.h"
#include "gt_mem.h"
#include "gt_engine.h"
#include "gt_lrc.h"
#include "gt_request.h"
#include "gt_submit.h"
#include "gt_resume.h"
#include "gt_defaults.h"
#include "gt_verify_wa.h"
#include "gt_migrate.h"
#include "pxp.h"
#include "driver_probe.h"
#include "eu_test.h"
#include "gt_init.h"
#include <drivers/dma.h>
#include "display_nogem.h"
#include "gt_mmio.h"
#include "gt_init.h"
#include "irq.h"
#include "display_core.h"
#include "timer_calc.h"
#include "dram_bw.h"
#include "osdep/pci.h"
#include "osdep/trace.h"
#include "osdep/runtime_pm.h"
#include <errno.h>

/*
 * Trace rings for the tests.  struct osdep_trace is 32 KiB -- far too big for
 * the 16 KiB kernel stack -- and the tests run serially on one thread, so two
 * shared static rings (one for the nested readout case) are enough.
 */
static struct osdep_trace ktest_trace_pool[2];

/* The P6C4B-WEDGE reset path takes the uncore lock. */
static struct spinlock ktest_wedge_lock;

static int g_fail;
static int g_checks;

#define KCHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { kern_logf("i915: parity ktest FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

static uint64_t
deadline_ms(unsigned ms)
{
	uint64_t d = 0;
	(void)kern_deadline_after(sched_ticks(), (uint64_t)ms * KERN_CLOCK_HZ / 1000u + 1u, &d);
	return d;
}

/* A handler for the MSI-split lifecycle test; the device never raises it. */
static void
msi_probe_handler(int irq, hal_irq_ack_t acknowledge, void *argument)
{
	(void)irq;
	(void)acknowledge;
	(void)argument;
}

/*
 * A fake osdep_mmio backend for the GPU-free reset / PCODE / DRAM tests.  It
 * models GEN6_GDRST and the PCODE mailbox against a small scripted register set,
 * using the existing osdep_mmio_backend vtable (no new test framework).
 */
struct fake_mmio_state {
	unsigned gdrst_fail_writes;   /* first N GDRST writes read back not-cleared */
	unsigned gdrst_writes;
	uint32_t mailbox, data, data1;
	const uint32_t (*script)[3];  /* [txn] = { val, val1, status } */
	unsigned script_len;
	unsigned txn;
	uint32_t pw_hsw_req, pw_aux_req, pw_ddi_req;      /* driver REQ bits (CTL2) */
	uint32_t pw_hsw_bios, pw_aux_bios, pw_ddi_bios;   /* BIOS REQ bits (CTL1) */
	int pw_no_ack;                                    /* if set, STATE never sets */
	int pcode_sticky_status;   /* if nonzero, every PCODE txn returns this status byte */
	int pcode_no_ready;        /* if set, MAILBOX keeps READY set (never acks) */
	unsigned pcode_txn_count;  /* MAILBOX transactions seen (additional-region tests) */
	int pcode_approve_when_preempt; /* reply READY only while preempt-disabled (additional region) */
	int pcode_reply_deny;      /* reply never satisfies the mask (force timeout / fault path) */
	uint32_t gen_off[96];      /* generic offset -> value store (combo PHY etc.) */
	uint32_t gen_val[96];
	unsigned gen_n;
	/* Per-transaction scripted PCODE: validates the command + input data,
	 * returns a per-txn status/response, and can delay the READY-clear so the
	 * wait is genuinely exercised.  NULL ptxn => fall back to script/sticky. */
	const struct fake_pcode_txn *ptxn;
	unsigned ptxn_len, ptxn_i;
	int ptxn_bad;              /* unexpected txn, command/input mismatch */
	uint32_t ptxn_pd, ptxn_pd1;
	uint8_t ptxn_pstatus;
	unsigned ptxn_rc;          /* MAILBOX reads left before READY clears */
	/* Ordered trace of register writes (operation-sequence verification). */
	uint32_t wt_off[96];
	uint32_t wt_val[96];
	unsigned wt_n;
	unsigned wt_total;   /* every write (uncapped) */
	/* SKL_FUSE_STATUS model: dist bits, optionally appearing after a delay. */
	uint32_t fuse_status;
	uint32_t fuse_delay_bits;
	unsigned fuse_delay;
};

/* VGA legacy-IO recorder: records the get -> read -> write -> put sequence. */
static struct { int seq[8]; unsigned n; unsigned char last_read, last_written; int got, put; } g_vga_rec;
static int ktest_vga_get(void *c, int r){ (void)c;(void)r; if (g_vga_rec.n<8u) g_vga_rec.seq[g_vga_rec.n++]=1; g_vga_rec.got=1; return 1; }
static unsigned char ktest_vga_in8(void *c, unsigned short port){ (void)c; if (g_vga_rec.n<8u) g_vga_rec.seq[g_vga_rec.n++]=2; g_vga_rec.last_read = (port==0x3CCu)?0xABu:0u; return g_vga_rec.last_read; }
static void ktest_vga_out8(void *c, unsigned short port, unsigned char v){ (void)c;(void)port; if (g_vga_rec.n<8u) g_vga_rec.seq[g_vga_rec.n++]=3; g_vga_rec.last_written=v; }
static void ktest_vga_put(void *c, int r){ (void)c;(void)r; if (g_vga_rec.n<8u) g_vga_rec.seq[g_vga_rec.n++]=4; g_vga_rec.put=1; }
static const struct parity_vga_io_ops vga_rec_ops = { ktest_vga_get, ktest_vga_in8, ktest_vga_out8, ktest_vga_put, 0 };

/* --- real preemption test: same-CPU A/B, IRQ-driven reschedule deferred while preempt off --- */
#define PT_CPU 1u
static struct wait_queue g_pt_wq;
static struct spinlock g_pt_lk;
static volatile int g_pt_b_ran, g_pt_b_sleeping, g_pt_fired;
static volatile int g_pt_after_fire, g_pt_after_inner, g_pt_after_outer, g_pt_ok, g_pt_done;

static void pt_oneshot(unsigned cpu, void *a)
{
	(void)cpu; (void)a;
	waitq_wake_one(&g_pt_wq);   /* wake B: runnable on PT_CPU, sets need_resched */
	g_pt_fired = 1;
}
static void pt_thread_b(void *a)
{
	uint64_t seq;
	(void)a;
	spin_lock(&g_pt_lk);
	g_pt_b_sleeping = 1;
	seq = waitq_sequence(&g_pt_wq);
	(void)waitq_sleep(&g_pt_wq, &g_pt_lk, seq, 0u, 0u);   /* sleep until woken */
	spin_unlock(&g_pt_lk);
	g_pt_b_ran++;
	g_pt_done = 1;
}
static void pt_spin_until(volatile int *flag)
{
	uint64_t b = 0, f = 0, now = 0, nf = 0, target;
	if (!kern_rtc_read_counter(&b, &f) || f == 0u) return;
	target = f / 5u;   /* ~200ms bound */
	while (*flag == 0) {
		if (!kern_rtc_read_counter(&now, &nf)) return;
		if (now - b >= target) return;
	}
}
static void pt_thread_a(void *a)
{
	(void)a;
	pt_spin_until(&g_pt_b_sleeping);   /* ensure B is parked on the wait queue */

	kern_preempt_disable();
	kern_preempt_disable();             /* nested: preempt_count = 2 */
	/* Fire a callback from the NEXT real timer IRQ on PT_CPU (avoid none). */
	kern_diag_oneshot_arm(pt_oneshot, 0, 0xffffffffu, PT_CPU);
	pt_spin_until(&g_pt_fired);          /* non-blocking wait: A never sleeps here */

	g_pt_after_fire = g_pt_b_ran;        /* reschedule requested, but switch is deferred */
	kern_preempt_enable();               /* 2 -> 1: still deferred */
	g_pt_after_inner = g_pt_b_ran;
	kern_preempt_enable();               /* 1 -> 0: processes the deferred switch -> B runs */
	g_pt_after_outer = g_pt_b_ran;       /* A resumes after B ran */
	g_pt_ok = 1;
}

/* Co-runner for the sleep-range yield test: makes progress while the main thread sleeps. */
static volatile unsigned long g_corun_counter;
static volatile int g_corun_stop;
static void sleep_corunner(void *a)
{
	(void)a;
	while (!g_corun_stop) {
		g_corun_counter++;
		kern_usleep_range(100u, 200u);   /* sub-tick busy wait, keeps it live */
	}
}

/* Monotonic scripted time; faults (freq change) once in the preempt-disabled region. */
static int pcode_time_ok(void *c, uint64_t *cnt, uint64_t *fr)
{
	static uint64_t k;
	(void)c;
	k += 1000u;
	*cnt = k;
	*fr = 1000000u;
	return 1;
}
static unsigned g_pcb_preempt_reads;
static int pcode_time_fault(void *c, uint64_t *cnt, uint64_t *fr)
{
	static uint64_t k;
	(void)c;
	k += 1000u;
	*cnt = k;
	*fr = 1000000u;
	if (sched_test_preempt_count() > 0u) {
		/* In the additional (preempt-off) region, fault the counter read after a
		 * couple of reads: kern_rtc_read_counter() fails -> the poll returns the
		 * time-base anomaly (-EIO), NOT a normal -ETIMEDOUT. */
		g_pcb_preempt_reads++;
		if (g_pcb_preempt_reads > 2u)
			return 0;
	}
	return 1;
}
static const struct parity_time_test_ops pcode_ok_ops = { pcode_time_ok, 0, 0, 0 };
static const struct parity_time_test_ops pcode_fault_ops = { pcode_time_fault, 0, 0, 0 };

/* --- DMC test helpers: corrupted-copy provider, fini-from-another-thread --- */
static uint8_t g_dmc_badcopy[79088];
static int dmc_bad_request(void *ctx, struct osdep_firmware *fw, const char *name)
{
	(void)ctx;
	if (name[0] == 'i' && name[5] == 'a' && name[6] == 'd' && name[10] == 'd') { /* i915/adlp_dmc.bin */
		fw->data = g_dmc_badcopy; fw->size = 79088u; return 0;
	}
	fw->data = 0; fw->size = 0u; return -ENOENT;
}
static const struct osdep_firmware_test_ops dmc_bad_ops = { dmc_bad_request, 0 };
static struct parity_dmc_dev *g_fini_dd;
static volatile int g_fini_done;
static void dmc_fini_thread(void *a)
{
	(void)a;
	kern_logf("i915: DMC-FINI thread: entering fini\n");
	parity_intel_dmc_fini(g_fini_dd, sched_ticks() + 300u);
	kern_logf("i915: DMC-FINI thread: fini returned\n");
	g_fini_done = 1;
}

/* --- P4 helpers: a scripted ISA-bridge list for intel_detect_pch() --- */
struct ktest_bridge { uint16_t vendor, device, svid, sdid; };
static const struct ktest_bridge *g_brs;
static unsigned g_brs_n;
static int ktest_bridge_next(void *ctx, unsigned i, uint16_t *v, uint16_t *d,
	uint16_t *sv, uint16_t *sd)
{
	(void)ctx;
	if (i >= g_brs_n)
		return 0;
	*v = g_brs[i].vendor; *d = g_brs[i].device;
	*sv = g_brs[i].svid;  *sd = g_brs[i].sdid;
	return 1;
}
static const struct parity_pch_bridge_ops ktest_bridge_ops = { ktest_bridge_next, 0 };

/* Time source that always faults (D-FAULT injection). */
static int ktest_fail_read(void *ctx, uint64_t *c, uint64_t *fr)
{ (void)ctx; (void)c; (void)fr; return 0; }

struct fake_pcode_txn {
	uint32_t exp_mbox;    /* expected command (mbox & ~READY); 0xffffffffu = any */
	uint32_t exp_data;    /* expected DATA written before MAILBOX; 0xffffffffu = any */
	uint32_t resp_data;   /* DATA returned to the reader */
	uint32_t resp_data1;
	uint8_t  status;      /* mailbox status byte once READY clears */
	uint8_t  ready_delay; /* MAILBOX reads that keep READY set first (0 = immediate) */
};

static void
fake_wt_record(struct fake_mmio_state *f, uint32_t off, uint32_t val)
{
	if (f->wt_n < 96u) { f->wt_off[f->wt_n] = off; f->wt_val[f->wt_n] = val; f->wt_n++; }
}

/* Order index of the first write to `off` whose value matches (val & mask); -1 if none. */
static int
fake_wt_find(struct fake_mmio_state *f, uint32_t off, uint32_t val, uint32_t mask)
{
	unsigned i;

	for (i = 0u; i < f->wt_n; i++)
		if (f->wt_off[i] == off && (f->wt_val[i] & mask) == (val & mask))
			return (int)i;
	return -1;
}

static uint32_t
fake_gen_get(struct fake_mmio_state *f, uint32_t off)
{
	unsigned i;

	for (i = 0u; i < f->gen_n; i++)
		if (f->gen_off[i] == off)
			return f->gen_val[i];
	return 0u;
}

static void
fake_gen_set(struct fake_mmio_state *f, uint32_t off, uint32_t val)
{
	unsigned i;

	for (i = 0u; i < f->gen_n; i++)
		if (f->gen_off[i] == off) { f->gen_val[i] = val; return; }
	if (f->gen_n < 96u) { f->gen_off[f->gen_n] = off; f->gen_val[f->gen_n] = val; f->gen_n++; }
}

static uint32_t
fake_raw_read32(void *priv, uint32_t off)
{
	struct fake_mmio_state *f = (struct fake_mmio_state *)priv;

	if (off == 0x42000u) { /* SKL_FUSE_STATUS */
		if (f->fuse_delay > 0u && --f->fuse_delay == 0u)
			f->fuse_status |= f->fuse_delay_bits;
		return f->fuse_status;
	}
	if (off == 0x941cu)   /* GEN6_GDRST */
		return (f->gdrst_writes <= f->gdrst_fail_writes) ? 0x1u : 0x0u;
	if (off == 0x138124u) { /* GEN6_PCODE_MAILBOX */
		if (f->ptxn != 0 && f->ptxn_rc > 0u) {
			f->ptxn_rc--;
			if (f->ptxn_rc == 0u) {
				f->data = f->ptxn_pd; f->data1 = f->ptxn_pd1;
				f->mailbox = (uint32_t)f->ptxn_pstatus;
			}
		}
		return f->mailbox;
	}
	if (off == 0x138128u) /* GEN6_PCODE_DATA */
		return f->data;
	if (off == 0x13812cu) /* GEN6_PCODE_DATA1 */
		return f->data1;
	if (off == 0x45404u || off == 0x45444u || off == 0x45454u) {
		uint32_t req  = (off == 0x45404u) ? f->pw_hsw_req  : (off == 0x45444u) ? f->pw_aux_req  : f->pw_ddi_req;
		uint32_t bios = (off == 0x45404u) ? f->pw_hsw_bios : (off == 0x45444u) ? f->pw_aux_bios : f->pw_ddi_bios;
		uint32_t state = f->pw_no_ack ? 0u : (((req | bios) & 0xAAAAAAAAu) >> 1);
		return req | state;
	}
	if (off == 0x45400u)
		return f->pw_hsw_bios;
	if (off == 0x45440u)
		return f->pw_aux_bios;
	if (off == 0x45450u)
		return f->pw_ddi_bios;
	return fake_gen_get(f, off);
}

static void
fake_raw_write32(void *priv, uint32_t off, uint32_t val)
{
	struct fake_mmio_state *f = (struct fake_mmio_state *)priv;

	f->wt_total++;
	fake_wt_record(f, off, val);

	if (off == 0x941cu) { f->gdrst_writes++; return; }
	if (off == 0x138128u) { f->data = val; return; }
	if (off == 0x13812cu) { f->data1 = val; return; }
	if (off == 0x138124u) {
		/* Per-transaction script (preferred): validate command + input data. */
		if (f->ptxn != 0) {
			uint32_t cmd = val & ~0x80000000u;
			const struct fake_pcode_txn *t;
			if (f->ptxn_i >= f->ptxn_len) { f->ptxn_bad = 1; f->mailbox = 0u; return; }
			t = &f->ptxn[f->ptxn_i];
			if ((t->exp_mbox != 0xffffffffu && cmd != t->exp_mbox) ||
			    (t->exp_data != 0xffffffffu && f->data != t->exp_data))
				f->ptxn_bad = 1;
			f->ptxn_pd = t->resp_data; f->ptxn_pd1 = t->resp_data1;
			f->ptxn_pstatus = t->status; f->ptxn_rc = t->ready_delay;
			f->ptxn_i++;
			if (f->ptxn_rc == 0u) {
				f->data = f->ptxn_pd; f->data1 = f->ptxn_pd1;
				f->mailbox = (uint32_t)f->ptxn_pstatus;
			} else {
				f->mailbox = 0x80000000u;   /* READY stays set until the countdown ends */
			}
			return;
		}
		/* MAILBOX write starts a transaction: load the scripted response and
		 * clear READY, leaving the status byte for the caller to inspect. */
		if (f->pcode_no_ready) {
			f->mailbox = 0x80000000u;   /* READY stays set: the poll never completes */
			return;
		}
		if (f->pcode_sticky_status != 0) {
			f->mailbox = (uint32_t)f->pcode_sticky_status & 0xffu;
			return;
		}
		f->pcode_txn_count++;
		if (f->pcode_reply_deny) {
			f->data = 0u; f->mailbox = 0u; return;   /* reply never matches */
		}
		if (f->pcode_approve_when_preempt) {
			/* Approve ONLY in the preempt-disabled additional region. */
			f->data = (sched_test_preempt_count() > 0u) ? 0x1u : 0x0u;
			f->mailbox = 0u; return;
		}
		if (f->txn < f->script_len) {
			f->data = f->script[f->txn][0];
			f->data1 = f->script[f->txn][1];
			f->mailbox = f->script[f->txn][2] & 0xffu;
			f->txn++;
		} else {
			f->mailbox = 0u;
		}
		return;
	}
	if (off == 0x45404u) { f->pw_hsw_req = val & 0xAAAAAAAAu; return; }
	if (off == 0x45444u) { f->pw_aux_req = val & 0xAAAAAAAAu; return; }
	if (off == 0x45454u) { f->pw_ddi_req = val & 0xAAAAAAAAu; return; }
	if (off == 0x45400u) { f->pw_hsw_bios = val & 0xAAAAAAAAu; return; }
	if (off == 0x45440u) { f->pw_aux_bios = val & 0xAAAAAAAAu; return; }
	if (off == 0x45450u) { f->pw_ddi_bios = val & 0xAAAAAAAAu; return; }
	if (off == 0x44FE8u || off == 0x44300u || off == 0x44304u || off == 0x44308u) {
		uint32_t v = val;   /* DBUF_CTL_S: POWER_STATE follows POWER_REQUEST */
		if (v & (1u << 31)) v |= (1u << 30); else v &= ~(1u << 30);
		fake_gen_set(f, off, v); return;
	}
	if (off == 0x46070u) {   /* BXT_DE_PLL_ENABLE: LOCK follows PLL_ENABLE, ACK follows FREQ_REQ */
		uint32_t v = val;
		if (v & (1u << 31)) v |= (1u << 30); else v &= ~(1u << 30);
		if (v & (1u << 23)) v |= (1u << 22); else v &= ~(1u << 22);
		fake_gen_set(f, off, v); return;
	}
	/* Combo PHY: a GRP write broadcasts to lane 0 (kept as distinct offsets). */
	if (off == 0x1626A0u) fake_gen_set(f, 0x1628A0u, val);        /* PHY_A TX_DW8 */
	else if (off == 0x162604u) fake_gen_set(f, 0x162804u, val);   /* PHY_A PCS_DW1 */
	else if (off == 0x6C6A0u) fake_gen_set(f, 0x6C8A0u, val);     /* PHY_B TX_DW8 */
	else if (off == 0x6C604u) fake_gen_set(f, 0x6C804u, val);     /* PHY_B PCS_DW1 */
	fake_gen_set(f, off, val);
}

static void fake_fw_request(void *priv, int domain, int wake)
{ (void)priv; (void)domain; (void)wake; }
static int fake_fw_ack(void *priv, int domain)
{ (void)priv; (void)domain; return 1; }

static const struct osdep_mmio_backend fake_mmio_backend = {
	"ktest-fake", fake_raw_read32, fake_raw_write32, fake_fw_request, fake_fw_ack
};

static void
fake_mmio_open(struct osdep_mmio *m, struct fake_mmio_state *f)
{
	unsigned i;

	for (i = 0u; i < sizeof(*f); i++)
		((char *)f)[i] = 0;
	osdep_mmio_init(m, &fake_mmio_backend, f, NULL, 0u, NULL);
}

/* DRM device management test fixtures (GPU-free). */
static struct parity_drm_device g_drm_test;
static int g_drm_action_ran;

static void
drm_test_action(void *arg)
{
	(void)arg;
	g_drm_action_ran++;
}

static void
drm_zero_fixture(void)
{
	unsigned i;

	for (i = 0u; i < sizeof(g_drm_test); i++)
		((char *)&g_drm_test)[i] = 0;
}

#if CONFIG_DRIVER_PCI_I915_PARITY
/* Diagnostic one-shot: a completion signalled from a real timer IRQ (T4). */
static struct parity_kcompletion g_oneshot_completion;
static volatile int g_oneshot_fired;
static volatile unsigned g_oneshot_cpu;
static volatile unsigned g_oneshot_tag;

static void
oneshot_timer_cb(unsigned cpu, void *arg)
{
	/* Runs in real timer-IRQ context, on a CPU other than the waiter's. */
	g_oneshot_cpu = cpu;
	g_oneshot_tag = (unsigned)(uintptr_t)arg;
	g_oneshot_fired = 1;
	parity_kcomplete(&g_oneshot_completion);
}
#endif

/* --- a worker thread that completes a completion once --- */
struct completer_ctx { struct parity_kcompletion *c; };
static void
completer_worker(void *arg)
{
	struct completer_ctx *w = (struct completer_ctx *)arg;
	parity_kcomplete(w->c);
}

static int
spawn_detached(void (*fn)(void *), void *arg)
{
	struct thread *thread;
	int rc = kthread_create(fn, arg, SCHED_PRIORITY_DEFAULT, &thread);

	if (rc != 0)
		return rc;
	thread->detached = 1;
	thread_start(thread);
	return 0;
}


static int
spawn_on_cpu(void (*fn)(void *), void *arg, unsigned cpu)
{
	struct thread *thread;
	int rc = kthread_create(fn, arg, SCHED_PRIORITY_DEFAULT, &thread);

	if (rc != 0)
		return rc;
	(void)sched_set_cpu(thread, (hal_cpu_id_t)cpu);
	thread->detached = 1;
	thread_start(thread);
	return 0;
}

/* --- WQ: self-requeue --- */
struct requeue_ctx { struct parity_kworkqueue *wq; struct parity_kwork *self; };
static void
wq_requeue_fn(void *p)
{
	struct requeue_ctx *c = (struct requeue_ctx *)p;
	if (c->self->ran_count < 2)
		(void)parity_kqueue_work(c->wq, c->self);
}

/* --- WQ: three-party cancel_work_sync --- */
static struct cancel_shared {
	struct parity_kcompletion worker_started;
	struct parity_kcompletion release;
	struct parity_kcompletion cancel_done;
	volatile int worker_returned;   /* set at the very end of the worker callback */
	volatile int order_ok;          /* worker_returned was 1 when cancel_work_sync returned */
} g_cancel;

static void
cancel_worker_fn(void *ctx)
{
	(void)ctx;
	parity_kcomplete(&g_cancel.worker_started);
	(void)parity_kwait(&g_cancel.release, deadline_ms(5000));
	g_cancel.worker_returned = 1;   /* callback is about to return */
}

struct canceller_ctx { struct parity_kworkqueue *wq; struct parity_kwork *w; };
static void
canceller_thread(void *arg)
{
	struct canceller_ctx *c = (struct canceller_ctx *)arg;
	(void)parity_kwait(&g_cancel.worker_started, deadline_ms(5000));
	(void)parity_kcancel_work_sync(c->wq, c->w, deadline_ms(5000));  /* blocks until callback returns */
	g_cancel.order_ok = g_cancel.worker_returned;                    /* must be 1 */
	parity_kcomplete(&g_cancel.cancel_done);
}

/* --- WQ: cross-CPU payload publish --- */
#define XCPU_GENS 64u
#define XCPU_MAGIC 0xA5A5A5A5u
static struct xcpu_shared {
	volatile uint32_t payload;
	volatile unsigned queuer_cpu;
	volatile unsigned worker_cpu;
	volatile uint32_t seen_payload;
	struct parity_kcompletion done;
} g_xcpu;

static void
xcpu_fn(void *ctx)
{
	uint32_t p;
	(void)ctx;
	/* Read the published payload FIRST, before any trace/notify. */
	p = g_xcpu.payload;
	g_xcpu.seen_payload = p;
	g_xcpu.worker_cpu = (unsigned)hal_cpu_current();
	parity_kcomplete(&g_xcpu.done);
}

/* intel_bios_init GPU-free fixtures: a ROM-less fake PCI + a synthetic VBT. */
static uint8_t bios_fpci_r8(void *pv, unsigned o) { (void)pv; (void)o; return 0u; }
static uint16_t bios_fpci_r16(void *pv, unsigned o) { (void)pv; (void)o; return 0u; }
static uint32_t bios_fpci_r32(void *pv, unsigned o) { (void)pv; (void)o; return 0u; }
static void bios_fpci_w8(void *pv, unsigned o, uint8_t v) { (void)pv; (void)o; (void)v; }
static void bios_fpci_w16(void *pv, unsigned o, uint16_t v) { (void)pv; (void)o; (void)v; }
static void bios_fpci_w32(void *pv, unsigned o, uint32_t v) { (void)pv; (void)o; (void)v; }
static int bios_fpci_alloc_msi(void *pv) { (void)pv; return -1; }
static void bios_fpci_free_msi(void *pv, int vec) { (void)pv; (void)vec; }
static const struct osdep_pci_backend bios_fpci_backend = {
	"bios-fake-pci",
	bios_fpci_r8, bios_fpci_r16, bios_fpci_r32,
	bios_fpci_w8, bios_fpci_w16, bios_fpci_w32,
	bios_fpci_alloc_msi, bios_fpci_free_msi,
};

static uint8_t g_vbt_buf[128];

/* Build a minimal VBT (48-byte header + BDB header + two blocks); size 81. */
static unsigned
bios_make_vbt(int good_sig)
{
	uint8_t *bdb;
	unsigned i;

	for (i = 0u; i < sizeof(g_vbt_buf); i++)
		g_vbt_buf[i] = 0u;
	g_vbt_buf[0] = good_sig ? (uint8_t)'$' : (uint8_t)'X';
	g_vbt_buf[1] = 'V'; g_vbt_buf[2] = 'B'; g_vbt_buf[3] = 'T';
	g_vbt_buf[20] = 0x00; g_vbt_buf[21] = 0x01;   /* version */
	g_vbt_buf[22] = 48u; g_vbt_buf[23] = 0u;      /* header_size */
	g_vbt_buf[24] = 81u; g_vbt_buf[25] = 0u;      /* vbt_size */
	g_vbt_buf[28] = 48u; g_vbt_buf[29] = 0u;      /* bdb_offset */
	bdb = g_vbt_buf + 48u;
	bdb[0] = 'B'; bdb[1] = 'D'; bdb[2] = 'B'; bdb[3] = ' ';
	bdb[16] = 200u; bdb[17] = 0u;                 /* bdb version */
	bdb[18] = 22u; bdb[19] = 0u;                  /* bdb header_size */
	bdb[20] = 33u; bdb[21] = 0u;                  /* bdb_size */
	bdb[22] = 1u; bdb[23] = 2u; bdb[24] = 0u;     /* block id=1 size=2 */
	bdb[25] = 0xAAu; bdb[26] = 0xBBu;
	bdb[27] = 2u; bdb[28] = 3u; bdb[29] = 0u;     /* block id=2 size=3 */
	bdb[30] = 0u; bdb[31] = 0u; bdb[32] = 0u;
	return 81u;
}

static void
bios_zero(struct parity_vbt_state *v)
{
	unsigned i;

	for (i = 0u; i < sizeof(*v); i++)
		((char *)v)[i] = 0;
}

/* Unit A: fake runtime-PM backend with a switchable resume result. */
static int g_rpm_fail;
static int rpm_test_resume(void *priv) { (void)priv; return g_rpm_fail ? -5 : 0; }
static void rpm_test_suspend(void *priv) { (void)priv; }
static const struct osdep_rpm_backend rpm_test_backend = {
	"test-rpm", rpm_test_resume, rpm_test_suspend,
};

/* Unit B: a scripted time source driving the real wait/udelay/reset bodies. */
struct time_test_ctx {
	unsigned reads;
	unsigned fail_after;      /* fail reads after this many (0 = never) */
	uint64_t freq;
	uint64_t freq2;
	unsigned freq_change_at;  /* switch to freq2 after this many reads (0 = never) */
	uint64_t counter;         /* fixed counter -> no overall timeout in these tests */
};
static int
time_test_read(void *cx, uint64_t *counter, uint64_t *freq)
{
	struct time_test_ctx *x = (struct time_test_ctx *)cx;

	x->reads++;
	if (x->fail_after != 0u && x->reads > x->fail_after)
		return 0;   /* counter read failure */
	*counter = x->counter;
	*freq = (x->freq_change_at != 0u && x->reads > x->freq_change_at) ? x->freq2 : x->freq;
	return 1;
}

int
parity_sync_ktest(void)
{
	static struct parity_kcompletion c;
	static struct completer_ctx wc;   /* large: keep off the 16 KiB stack */
	unsigned cpu = (unsigned)hal_cpu_current();

	g_fail = 0;
	g_checks = 0;
	kern_logf("i915: parity ktest begin (cpu=%u, shared backend)\n", cpu);
	(void)parity_wait_time_base_ok(); /* cache the TSC frequency while it is fresh */

	/* K0: native waitq + timer deadline, independent of the completion object. */
	{
		static struct spinlock lock;
		static struct wait_queue wq;
		uint64_t start_tick, dl, end_tick;
		int rc;

		spin_init(&lock, LOCK_RANK_DEVICE, "ktest-k0");
		waitq_init(&wq, "ktest-k0");
		start_tick = sched_ticks();
		dl = deadline_ms(200);
		spin_lock(&lock);
		rc = waitq_sleep(&wq, &lock, waitq_sequence(&wq), dl, 0u);
		spin_unlock(&lock);
		end_tick = sched_ticks();
		kern_logf("i915: parity ktest K0 native-waitq start=%llu deadline=%llu end=%llu rc=%d\n",
			(unsigned long long)start_tick, (unsigned long long)dl,
			(unsigned long long)end_tick, rc);
		KCHECK(end_tick >= dl, "K0: timer advanced past the deadline (native wait woke)");
	}

	/* K1: complete before wait -> immediate. */
	parity_kcompletion_init(&c, "ktest-1");
	parity_kcomplete(&c);
	KCHECK(parity_kwait(&c, deadline_ms(1000)) == 1, "K1 complete-before-wait");

	/* K2: counting. */
	parity_kcompletion_init(&c, "ktest-2");
	parity_kcomplete(&c);
	parity_kcomplete(&c);
	KCHECK(parity_kwait(&c, deadline_ms(1000)) == 1, "K2 first wait");
	KCHECK(parity_kwait(&c, deadline_ms(1000)) == 1, "K2 second wait");
	KCHECK(parity_kwait(&c, deadline_ms(200)) == 0, "K2 third wait times out");

	/* K3: blocking wait woken by another thread. */
	parity_kcompletion_init(&c, "ktest-3");
	wc.c = &c;
	KCHECK(spawn_detached(completer_worker, &wc) == 0, "K3 spawn completer");
	KCHECK(parity_kwait(&c, deadline_ms(5000)) == 1, "K3 blocking wait woken by thread");

	/* K4: timeout when never completed. */
	parity_kcompletion_init(&c, "ktest-4");
	KCHECK(parity_kwait(&c, deadline_ms(200)) == 0, "K4 timeout");

	/* K5: complete_all opens permanently. */
	parity_kcompletion_init(&c, "ktest-5");
	parity_kcomplete_all(&c);
	KCHECK(parity_kwait(&c, deadline_ms(1000)) == 1, "K5 wait 1");
	KCHECK(parity_kwait(&c, deadline_ms(1000)) == 1, "K5 wait 2 (not consumed)");

	/* ===================== workqueue (shared backend) ===================== */
	{
		static struct parity_kworkqueue wq;
		static struct parity_kwork warr[XCPU_GENS];
		static struct parity_kwork w;   /* large: keep off the 16 KiB stack */
		static struct requeue_ctx rq;   /* large: keep off the 16 KiB stack */
		static struct canceller_ctx cc;   /* large: keep off the 16 KiB stack */
		unsigned gen, diff_cpu = 0u, mismatch = 0u, queue_fail = 0u;

		KCHECK(parity_kworkqueue_create(&wq, "ktest-wq") == 0, "WQ create worker thread");

		/* WQ-requeue: a running work re-queues itself once -> runs twice. */
		parity_kwork_init(&w, wq_requeue_fn, &rq);
		rq.wq = &wq; rq.self = &w;
		parity_kqueue_work(&wq, &w);
		{
			static struct parity_kcompletion idle;   /* large: keep off the 16 KiB stack */
			parity_kcompletion_init(&idle, "wq-idle");
			/* poll for two runs (bounded) */
			for (gen = 0u; gen < 200u && w.ran_count < 2; gen++)
				parity_kwait(&idle, deadline_ms(10));
		}
		KCHECK(w.ran_count == 2, "WQ-requeue: self-requeue ran twice");

		/* WQ cancel_work_sync: worker blocks; canceller's sync-cancel waits for it. */
		parity_kcompletion_init(&g_cancel.worker_started, "cs-start");
		parity_kcompletion_init(&g_cancel.release, "cs-release");
		parity_kcompletion_init(&g_cancel.cancel_done, "cs-done");
		g_cancel.worker_returned = 0;
		g_cancel.order_ok = 0;
		parity_kwork_init(&w, cancel_worker_fn, 0);
		parity_kqueue_work(&wq, &w);
		KCHECK(parity_kwait(&g_cancel.worker_started, deadline_ms(5000)) == 1, "cancel: worker started");
		cc.wq = &wq; cc.w = &w;
		KCHECK(spawn_detached(canceller_thread, &cc) == 0, "cancel: spawn canceller");
		/* give the canceller time to enter cancel_work_sync (which blocks) */
		{ struct parity_kcompletion s; parity_kcompletion_init(&s, "cs-settle"); parity_kwait(&s, deadline_ms(100)); }
		parity_kcomplete(&g_cancel.release);   /* worker returns -> sync-cancel returns */
		KCHECK(parity_kwait(&g_cancel.cancel_done, deadline_ms(5000)) == 1, "cancel: canceller finished");
		KCHECK(g_cancel.order_ok == 1, "cancel_work_sync did not return before the callback finished");

		/* cross-CPU payload publish: writer stores payload then queues; worker reads it. */
		parity_kcompletion_init(&g_xcpu.done, "xcpu");
		for (gen = 1u; gen <= XCPU_GENS; gen++) {
			parity_kwork_init(&warr[gen - 1u], xcpu_fn, 0);
			parity_kreinit_completion(&g_xcpu.done);
			g_xcpu.payload = gen ^ XCPU_MAGIC;               /* publish BEFORE queue */
			g_xcpu.queuer_cpu = (unsigned)hal_cpu_current();
			g_xcpu.worker_cpu = 0xffffu;
			g_xcpu.seen_payload = 0u;
			if (parity_kqueue_work(&wq, &warr[gen - 1u]) != 1) { queue_fail++; continue; }
			parity_kwait(&g_xcpu.done, deadline_ms(5000));
			if (g_xcpu.seen_payload != (gen ^ XCPU_MAGIC)) mismatch++;
			if (g_xcpu.worker_cpu != g_xcpu.queuer_cpu) diff_cpu++;
		}
		kern_logf("i915: parity ktest cross-CPU: gens=%u cross_cpu=%u mismatch=%u queue_fail=%u\n",
			XCPU_GENS, diff_cpu, mismatch, queue_fail);
		KCHECK(mismatch == 0u, "cross-CPU: worker saw the published payload every generation");
		KCHECK(queue_fail == 0u, "cross-CPU: every queue succeeded");

		parity_kworkqueue_destroy(&wq);
		KCHECK(1, "WQ worker reclaimed on destroy");
	}

	/* --- HAL C3: MSI-split vector/handler lifecycle (GPU-free) --- */
	{
		int irq_a = -1;
		int irq_b = -1;
		paddr_t addr_a = 0;
		paddr_t addr_b = 0;
		uint32_t ev_a = 0;
		uint32_t ev_b = 0;

		/* M0: allocate a vector with no handler, then release it. */
		KCHECK(hal_irq_alloc_msi("PCI ffff:ff:1f.7", &irq_a, &addr_a, &ev_a) == HAL_OK,
			"MSI M0: alloc_msi reserves a vector");
		KCHECK(addr_a != 0, "MSI M0: alloc_msi returns a message address");
		KCHECK(hal_irq_free_msi(irq_a) == HAL_OK,
			"MSI M0: free_msi releases an unattached vector");

		/* M1: attach validates its handler argument. */
		KCHECK(hal_irq_alloc_msi("PCI ffff:ff:1f.7", &irq_a, &addr_a, &ev_a) == HAL_OK,
			"MSI M1: alloc");
		KCHECK(hal_irq_attach_msi(irq_a, NULL, NULL) == HAL_ERR_INVALID,
			"MSI M1: attach rejects a NULL handler");
		KCHECK(hal_irq_free_msi(irq_a) == HAL_OK, "MSI M1: free after a failed attach");

		/* M2: full attach -> detach_sync -> free lifecycle with state gates. */
		KCHECK(hal_irq_alloc_msi("PCI ffff:ff:1f.7", &irq_a, &addr_a, &ev_a) == HAL_OK,
			"MSI M2: alloc");
		KCHECK(hal_irq_attach_msi(irq_a, msi_probe_handler, &g_checks) == HAL_OK,
			"MSI M2: attach a handler");
		KCHECK(hal_irq_free_msi(irq_a) == HAL_ERR_STATE,
			"MSI M2: free is refused while a handler is attached");
		KCHECK(hal_irq_detach_msi_sync(irq_a, msi_probe_handler, &g_fail) == HAL_ERR_INVALID,
			"MSI M2: detach rejects a mismatched registration");
		KCHECK(hal_irq_detach_msi_sync(irq_a, msi_probe_handler, &g_checks) == HAL_OK,
			"MSI M2: detach_sync drains the exact registration");
		KCHECK(hal_irq_free_msi(irq_a) == HAL_OK, "MSI M2: free after detach");

		/* M3: two live allocations occupy distinct vectors. */
		KCHECK(hal_irq_alloc_msi("PCI ffff:ff:1f.7", &irq_a, &addr_a, &ev_a) == HAL_OK,
			"MSI M3: alloc A");
		KCHECK(hal_irq_alloc_msi("PCI ffff:ff:1f.7", &irq_b, &addr_b, &ev_b) == HAL_OK,
			"MSI M3: alloc B");
		KCHECK(irq_a != irq_b, "MSI M3: distinct allocations get distinct vectors");
		KCHECK(hal_irq_free_msi(irq_a) == HAL_OK, "MSI M3: free A");
		KCHECK(hal_irq_free_msi(irq_b) == HAL_OK, "MSI M3: free B");

		/* M4: operations on an unallocated vector are refused. */
		KCHECK(hal_irq_free_msi(irq_a) == HAL_ERR_INVALID,
			"MSI M4: free of an already-released vector is refused");
		KCHECK(hal_irq_detach_msi_sync(irq_a, msi_probe_handler, &g_checks) == HAL_ERR_INVALID,
			"MSI M4: detach of an unallocated vector is refused");
	}

	/* --- HAL C3: write-combining attribute contract (GPU-free) --- */
	{
		void *va = NULL;

		/* Conflicting cache policies are rejected before any mapping occurs. */
		KCHECK(hal_space_map_device(0xfed00000ull, 0x1000u,
			HAL_SPACE_READ | HAL_SPACE_WC | HAL_SPACE_NOCACHE, &va) == HAL_ERR_INVALID,
			"WC: a WC request combined with NOCACHE is rejected");
		KCHECK(va == NULL, "WC: a rejected request leaves the output pointer untouched");
		KCHECK(hal_space_map_device(0xfed00000ull, 0x1000u,
			HAL_SPACE_READ | HAL_SPACE_WC | HAL_SPACE_DEVICE, &va) == HAL_ERR_INVALID,
			"WC: a WC request combined with DEVICE is rejected");
	}

	/* --- P1 reset control flow (mock MMIO, real-time waits) --- */
	{
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct spinlock tl;

		spin_init(&tl, LOCK_RANK_DEVICE, "ktest-uncore");

		fake_mmio_open(&m, &f);
		f.gdrst_fail_writes = 0u;
		KCHECK(parity_gt_reset_all(&tl, &m, 2000u) == 0,
			"reset: succeeds when the domain acks");

		fake_mmio_open(&m, &f);
		f.gdrst_fail_writes = 999u;
		KCHECK(parity_gt_reset_all(&tl, &m, 2000u) == -ETIMEDOUT,
			"reset: times out when the ack never arrives");

		fake_mmio_open(&m, &f);
		f.gdrst_fail_writes = 1u;
		KCHECK(parity_gt_reset_all(&tl, &m, 2000u) == 0,
			"reset: recovers on a retry after one failed attempt");
	}

	/* --- PCODE transaction (mock MMIO) --- */
	{
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct mutex tsb;
		static const uint32_t ok_script[1][3] = { { 0x0000abcdu, 0u, 0x0u } };
		static const uint32_t err_script[1][3] = { { 0u, 0u, 0x2u } }; /* GEN7 timeout */
		uint32_t val = 0;

		(void)mutex_init(&tsb, LOCK_RANK_DEVICE, "ktest-sb");

		fake_mmio_open(&m, &f);
		f.script = ok_script; f.script_len = 1u;
		KCHECK(parity_pcode_read(&tsb, &m, 0x0000000du, &val, 0) == 0 && val == 0x0000abcdu,
			"pcode: success returns the response word");

		fake_mmio_open(&m, &f);
		f.mailbox = 0x80000000u;
		KCHECK(parity_pcode_read(&tsb, &m, 0x0000000du, &val, 0) == -EAGAIN,
			"pcode: a busy mailbox yields -EAGAIN");

		fake_mmio_open(&m, &f);
		f.script = err_script; f.script_len = 1u;
		KCHECK(parity_pcode_read(&tsb, &m, 0x0000000du, &val, 0) == -ETIMEDOUT,
			"pcode: a mailbox timeout status maps to -ETIMEDOUT");
	}

	/* --- DRAM global-info decode (pure) --- */
	{
		struct parity_dram_info di;
		unsigned i;

		for (i = 0u; i < sizeof(di); i++)
			((char *)&di)[i] = 0;
		KCHECK(parity_dram_decode(0x00003420u, &di) == 0,
			"dram_decode: valid word decodes");
		KCHECK(di.type == (int)PARITY_DRAM_DDR4 && di.num_channels == 2u &&
			di.num_qgv_points == 4u && di.num_psf_gv_points == 3u,
			"dram_decode: DDR4/2ch/4qgv/3psf fields");
		KCHECK(parity_dram_decode(0x0000000fu, &di) == -EINVAL,
			"dram_decode: an unknown type field is rejected");
	}

	/* --- DRAM detect + bandwidth: verify the SAVED device state --- */
	{
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct mutex tsb;
		struct parity_dram_info di;
		struct parity_bw_state bw;
		static const uint32_t ok_seq[6][3] = {
			{ 0x00003420u, 0u, 0u },
			{ 0x000f03e8u, 0x00000804u, 0u }, { 0x000f03e8u, 0x00000804u, 0u },
			{ 0x000f03e8u, 0x00000804u, 0u }, { 0x000f03e8u, 0x00000804u, 0u },
			{ 0x00302010u, 0u, 0u },
		};
		static const uint32_t psf_fail_seq[6][3] = {
			{ 0x00003420u, 0u, 0u },
			{ 0x000f03e8u, 0x00000804u, 0u }, { 0x000f03e8u, 0x00000804u, 0u },
			{ 0x000f03e8u, 0x00000804u, 0u }, { 0x000f03e8u, 0x00000804u, 0u },
			{ 0u, 0u, 0x1u },
		};
		unsigned i;

		(void)mutex_init(&tsb, LOCK_RANK_DEVICE, "ktest-sb2");

		fake_mmio_open(&m, &f);
		f.script = ok_seq; f.script_len = 6u;
		for (i = 0u; i < sizeof(di); i++) ((char *)&di)[i] = 0;
		for (i = 0u; i < sizeof(bw); i++) ((char *)&bw)[i] = 0;
		KCHECK(parity_dram_detect(&tsb, &m, &di) == 0 && di.num_qgv_points == 4u,
			"dram_detect: decodes global info over PCODE");
		KCHECK(parity_bw_init_hw(&tsb, &m, &di, &bw) == 0, "bw_init: returns success");
		KCHECK(bw.valid == 1 && bw.sagv_status == (int)PARITY_SAGV_ENABLED,
			"bw_init: saved SAGV status is enabled");
		KCHECK(bw.max[0].num_qgv_points == 4u && bw.max[0].num_psf_gv_points == 3u,
			"bw_init: saved per-group point counts");
		KCHECK(bw.max[0].deratedbw[0] != 0u && bw.max[0].peakbw[0] != 0u,
			"bw_init: saved group-0 bandwidth values");
		KCHECK(bw.max[0].num_planes == 0u && bw.max[1].num_planes != 0u,
			"bw_init: num_planes stored into the NEXT group");

		fake_mmio_open(&m, &f);
		f.script = psf_fail_seq; f.script_len = 6u;
		for (i = 0u; i < sizeof(di); i++) ((char *)&di)[i] = 0;
		for (i = 0u; i < sizeof(bw); i++) ((char *)&bw)[i] = 0;
		(void)parity_dram_detect(&tsb, &m, &di);
		KCHECK(parity_bw_init_hw(&tsb, &m, &di, &bw) == 0,
			"bw_init: a PSF read failure is tolerated");
		KCHECK(bw.max[0].num_psf_gv_points == 0u,
			"bw_init: PSF failure reflected in the saved num_psf_gv_points");
	}

	/* --- WC full-range aperture map failure + recovery (real HAL) --- */
	{
		void *va = (void *)0x1;
		void *va2 = NULL;
		int rc1, rc2;

		rc1 = hal_space_map_device(0xfed00000ull, 0x40000000u,
			HAL_SPACE_READ | HAL_SPACE_WC, &va);
		KCHECK(rc1 != HAL_OK, "WC: an oversized aperture map is refused");
		KCHECK(va == (void *)0x1, "WC: a refused map leaves the output untouched");

		rc2 = hal_space_map_device(0xfed00000ull, 0x1000u,
			HAL_SPACE_READ | HAL_SPACE_WC, &va2);
		KCHECK(rc2 == HAL_OK && va2 != NULL,
			"WC: a valid map succeeds after the failed one (recovery)");
		if (rc2 == HAL_OK)
			(void)hal_space_unmap_device(va2, 0x1000u);
	}

#if CONFIG_DRIVER_PCI_I915_PARITY
	/* --- IRQ-context completion one-shot (real timer IRQ, cross-CPU) --- */
	{
		unsigned self = (unsigned)hal_cpu_current();

		if (hal_cpu_count() < 2u) {
			KCHECK(1, "IRQ oneshot: skipped (needs SMP)");
		} else {
			parity_kcompletion_init(&g_oneshot_completion, "ktest-oneshot");
			g_oneshot_fired = 0;
			g_oneshot_cpu = 0xffffffffu;
			g_oneshot_tag = 0u;
			kern_diag_oneshot_arm(oneshot_timer_cb, (void *)(uintptr_t)0xabcdu, self, 0xffffffffu);
			KCHECK(parity_kwait(&g_oneshot_completion, deadline_ms(3000)) == 1,
				"IRQ oneshot: shared completion signalled from a timer IRQ (cross-CPU)");
			KCHECK(g_oneshot_fired == 1 && g_oneshot_tag == 0xabcdu && g_oneshot_cpu != self,
				"IRQ oneshot: fired once in IRQ context, cross-CPU, with the published tag");
			kern_diag_oneshot_disarm();

			/* Same-CPU: notify from the waiter's OWN CPU timer IRQ (IRQ-safe backend). */
			self = (unsigned)hal_cpu_current();
			parity_kreinit_completion(&g_oneshot_completion);
			g_oneshot_fired = 0;
			g_oneshot_cpu = 0xffffffffu;
			g_oneshot_tag = 0u;
			kern_diag_oneshot_arm(oneshot_timer_cb, (void *)(uintptr_t)0x1234u, 0xffffffffu, self);
			KCHECK(parity_kwait(&g_oneshot_completion, deadline_ms(3000)) == 1,
				"IRQ oneshot: completion signalled from the waiter's OWN CPU timer IRQ");
			KCHECK(g_oneshot_fired == 1 && g_oneshot_tag == 0x1234u && g_oneshot_cpu == self,
				"IRQ oneshot: fired in IRQ context on the SAME CPU as the waiter");
			kern_diag_oneshot_disarm();
		}
	}

#endif
	/* --- DRM device management + drm_vblank_init, per-pipe workers (GPU-free) --- */
	{
		unsigned k;

		/* Normal init: 4 CRTCs, each with its OWN worker; managed cleanup at fini. */
		drm_zero_fixture();
		KCHECK(parity_drm_dev_init(&g_drm_test, 0, 0x3u) == 0, "drm: dev_init");
		KCHECK(parity_drm_vblank_init(&g_drm_test, 4u) == 0 &&
			g_drm_test.num_crtcs == 4u && g_drm_test.vblank_inited == 1,
			"drm: vblank_init 4 CRTCs");
		KCHECK(g_drm_test.vblank[0].inited == 1 && g_drm_test.vblank[0].worker_created == 1 &&
			g_drm_test.vblank[3].pipe == 3u && g_drm_test.vblank[3].worker_created == 1,
			"drm: per-CRTC state + per-pipe worker");
		parity_drm_dev_fini(&g_drm_test);
		KCHECK(g_drm_test.vblank_inited == 0 && g_drm_test.inited == 0 &&
			g_drm_test.vblank[0].worker_created == 0 &&
			g_drm_test.vblank[3].worker_created == 0,
			"drm: dev_fini tears down every per-pipe worker");

		/* Array bounds: invalid CRTC counts are rejected without partial state. */
		drm_zero_fixture();
		(void)parity_drm_dev_init(&g_drm_test, 0, 0);
		KCHECK(parity_drm_vblank_init(&g_drm_test, 0u) == -1, "drm: reject 0 CRTCs");
		KCHECK(parity_drm_vblank_init(&g_drm_test, PARITY_DRM_MAX_PIPES + 1u) == -1 &&
			g_drm_test.vblank_inited == 0,
			"drm: reject a CRTC count over the pipe array");
		parity_drm_dev_fini(&g_drm_test);

		/* drm_add_action_or_reset: a full list runs the action immediately. */
		drm_zero_fixture();
		(void)parity_drm_dev_init(&g_drm_test, 0, 0);
		for (k = 0u; k < PARITY_DRMM_MAX; k++)
			(void)parity_drmm_add_action_or_reset(&g_drm_test, drm_test_action, 0);
		g_drm_action_ran = 0;
		KCHECK(parity_drmm_add_action_or_reset(&g_drm_test, drm_test_action, 0) == -1 &&
			g_drm_action_ran == 1,
			"drm: add_action_or_reset runs the action on a full list");
		parity_drm_dev_fini(&g_drm_test);

		/* Mid-init recovery A: an unregistrable CRTC cleanup unwinds the whole init. */
		drm_zero_fixture();
		(void)parity_drm_dev_init(&g_drm_test, 0, 0);
		for (k = 0u; k < PARITY_DRMM_MAX; k++)
			(void)parity_drmm_add_action_or_reset(&g_drm_test, drm_test_action, 0);
		KCHECK(parity_drm_vblank_init(&g_drm_test, 4u) == -1 &&
			g_drm_test.vblank_inited == 0,
			"drm: vblank_init unwinds when a CRTC cleanup cannot be registered");
		parity_drm_dev_fini(&g_drm_test);

		/* Mid-init recovery B: a per-pipe worker-create failure unwinds cleanly. */
		drm_zero_fixture();
		(void)parity_drm_dev_init(&g_drm_test, 0, 0);
		parity_drm_vblank_test_fail_worker_at(2);   /* pipe 2's worker will not start */
		KCHECK(parity_drm_vblank_init(&g_drm_test, 4u) == -1 &&
			g_drm_test.vblank_inited == 0 && g_drm_test.num_crtcs == 2u,
			"drm: vblank_init unwinds on a per-pipe worker-create failure");
		parity_drm_dev_fini(&g_drm_test);
		KCHECK(g_drm_test.vblank[0].worker_created == 0 &&
			g_drm_test.vblank[1].worker_created == 0,
			"drm: dev_fini reclaims workers created before the failed pipe");
	}

	/* --- intel_bios_init: VBT validate / parse / genuine-absence defaults --- */
	{
		struct parity_vbt_state vbt;
		struct osdep_pci fpci;
		struct osdep_trace *ftr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		unsigned vlen;

		osdep_trace_init(ftr);
		osdep_pci_init(&fpci, &bios_fpci_backend, 0, ftr);

		vlen = bios_make_vbt(1);
		KCHECK(parity_bios_is_valid_vbt(g_vbt_buf, vlen) == 1,
			"bios: is_valid_vbt accepts a good VBT");
		KCHECK(parity_bios_is_valid_vbt(0, vlen) == 0,
			"bios: is_valid_vbt rejects NULL");
		KCHECK(parity_bios_is_valid_vbt(g_vbt_buf, 10u) == 0,
			"bios: is_valid_vbt rejects a short buffer");
		(void)bios_make_vbt(0);
		KCHECK(parity_bios_is_valid_vbt(g_vbt_buf, vlen) == 0,
			"bios: is_valid_vbt rejects a bad signature");

		vlen = bios_make_vbt(1);
		bios_zero(&vbt);
		KCHECK(parity_bios_process_vbt(&vbt, g_vbt_buf, vlen) == 0 &&
			vbt.version == 200u && vbt.num_bdb_blocks == 2u && vbt.vbt_found == 1,
			"bios: process_vbt parses BDB header + walks blocks");

		bios_zero(&vbt);
		parity_bios_init_vbt_missing_defaults(&vbt);
		KCHECK(vbt.num_display_devices == 3u && vbt.version == 155u,
			"bios: missing defaults generate 3 non-TC DDI children");
		KCHECK(vbt.display_devices[0].port == 0u &&
			(vbt.display_devices[0].device_type & (1u << 12)) != 0u &&
			(vbt.display_devices[0].device_type & (1u << 4)) == 0u,
			"bios: PORT_A child is internal-connector and not TMDS");

		bios_zero(&vbt);
		KCHECK(parity_intel_bios_init(&vbt, &fpci, 0, ftr) == 0 &&
			vbt.vbt_found == 0 && vbt.missing_defaults_used == 1 &&
			vbt.num_display_devices == 3u && vbt.source == PARITY_VBT_SRC_NONE,
			"bios: intel_bios_init falls back to defaults when VBT is absent");
	}

	/* --- intel_vga_register decode + power-domain map + pmdemand (GPU-free) --- */
	{
		static struct parity_power_domains pd;
		struct parity_pmdemand pm;
		struct parity_vga_client vga;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		uint64_t w;

		osdep_trace_init(tr);

		/* VGA decode resource flags (no bridge -> set_state is a no-op -ENODEV). */
		vga.gpu = 0; vga.gmch = 0; vga.display_ver = 13u; vga.registered = 0;
		KCHECK(parity_intel_gmch_vga_set_decode(&vga, 1) ==
			(PARITY_VGA_RSRC_LEGACY_IO | PARITY_VGA_RSRC_LEGACY_MEM |
			 PARITY_VGA_RSRC_NORMAL_IO | PARITY_VGA_RSRC_NORMAL_MEM),
			"vga: decode enable returns legacy+normal IO/MEM");
		KCHECK(parity_intel_gmch_vga_set_decode(&vga, 0) ==
			(PARITY_VGA_RSRC_NORMAL_IO | PARITY_VGA_RSRC_NORMAL_MEM),
			"vga: decode disable returns normal IO/MEM only");
		KCHECK(parity_intel_gmch_vga_set_state(&vga, 1) == -ENODEV,
			"vga: set_state without a GMCH bridge is -ENODEV");

		/* intel_power_domains_init: xelpd map + DC mask. */
		KCHECK(parity_intel_power_domains_init(&pd, 13u, -1, -1, tr) == 0 &&
			pd.num_power_wells == 30u &&
			pd.allowed_dc_mask == 0x4000000au &&
			pd.target_dc_state == 0x00000002u,
			"power: domains_init builds xelpd map (30 wells) + DC mask 0x4000000a/0x2");
		w = parity_power_domain_wells(&pd, PARITY_PW_DOMAIN_PIPE_A);
		KCHECK((w & ((uint64_t)1u << 0)) != 0u && (w & ((uint64_t)1u << 4)) != 0u,
			"power: PIPE_A -> always_on + PW_A");
		w = parity_power_domain_wells(&pd, PARITY_PW_DOMAIN_DC_OFF);
		KCHECK((w & ((uint64_t)1u << 0)) != 0u && (w & ((uint64_t)1u << 2)) != 0u,
			"power: DC_OFF -> always_on + DC_off well");
		KCHECK(pd.power_wells[3].has_vga == 1 && pd.power_wells[3].always_on == 0 &&
			pd.power_wells[0].always_on == 1,
			"power: PW_2 has_vga (not always_on); always_on well flagged");
		KCHECK(pd.power_wells[4].refcount == 0u && pd.power_wells[4].hw_enabled == -1,
			"power: live state (refcount/hw_enabled) separate from descriptor");
		/* AUX_TBT1..4 present (were missing); is_tc_tbt + reference hsw.idx. */
		KCHECK(pd.power_wells[29].is_tc_tbt == 1 && pd.power_wells[29].hsw_idx == 12u &&
			pd.power_wells[26].is_tc_tbt == 1,
			"power: AUX_TBT4/TBT1 present with is_tc_tbt + hsw.idx=12");
		/* AUX_USBC1: fixed_enable_delay + WA enable_timeout=500ms; reference idx. */
		KCHECK(pd.power_wells[22].fixed_enable_delay == 1 &&
			pd.power_wells[22].enable_timeout == 500u && pd.power_wells[22].hsw_idx == 3u,
			"power: AUX_USBC1 fixed_enable_delay + enable_timeout=500 + idx=3");
		/* Reference ids + hsw.idx (NOT inferred from array position). */
		KCHECK(parity_power_well_by_id(&pd, PARITY_SKL_DISP_PW_2) == 3 &&
			parity_power_well_by_id(&pd, PARITY_SKL_DISP_PW_1) == 1 &&
			pd.power_wells[3].hsw_idx == 1u && pd.power_wells[4].hsw_idx == 5u,
			"power: PW_1/PW_2 found by id; PW_2 idx=1, PW_A idx=5");
		/* NULL domain list (PW_1) = none; zero-length (always_on) = all -- distinct. */
		KCHECK(pd.power_wells[0].domains_all == 1 && pd.power_wells[1].domains_all == 0 &&
			parity_power_domain_wells(&pd, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC4) != 0u,
			"power: always_on=all-domains, PW_1=NULL-list(none)");
		parity_intel_power_domains_cleanup(&pd);
		KCHECK(pd.initialized == 0, "power: cleanup releases the map");

		/* intel_pmdemand_init_early. */
		pm.early_initialized = 0;
		parity_intel_pmdemand_init_early(&pm);
		KCHECK(pm.early_initialized == 1,
			"pmdemand: init_early sets up lock + wait queue");
	}

	/* --- PCI probe runtime PM contract (unit A, GPU-free) --- */
	{
		struct osdep_rpm pm;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		int rc;

		osdep_trace_init(tr);

		/* Normal get_sync: usage++, resume OK, device active. */
		g_rpm_fail = 0;
		osdep_rpm_init_early(&pm, &rpm_test_backend, 0, tr);
		rc = osdep_rpm_get_sync(&pm);
		KCHECK(rc == 0 && osdep_rpm_usage(&pm) == 1 && osdep_rpm_active(&pm) == 1,
			"rpm: get_sync resumes and holds a usage reference");
		osdep_rpm_put(&pm);
		KCHECK(osdep_rpm_usage(&pm) == 0, "rpm: put drops the usage reference");

		/* get_sync on resume failure: returns <0 but LEAVES the usage count. */
		g_rpm_fail = 1;
		osdep_rpm_init_early(&pm, &rpm_test_backend, 0, tr);
		pm.active = 0;   /* force do_resume to call the backend (resume needed) */
		rc = osdep_rpm_get_sync(&pm);
		KCHECK(rc < 0 && osdep_rpm_usage(&pm) == 1,
			"rpm: get_sync leaves usage incremented on resume failure (caller must put)");
		osdep_rpm_put(&pm);

		/* resume_and_get on resume failure: returns <0 and UNWINDS the usage count. */
		g_rpm_fail = 1;
		osdep_rpm_init_early(&pm, &rpm_test_backend, 0, tr);
		pm.active = 0;   /* force do_resume to call the backend (resume needed) */
		rc = osdep_rpm_resume_and_get(&pm);
		KCHECK(rc < 0 && osdep_rpm_usage(&pm) == 0,
			"rpm: resume_and_get unwinds usage on failure (distinct from get_sync)");
		g_rpm_fail = 0;
	}

	/* --- Time-source fault handling (unit B, tests 2-4; test 1 = HAL CAS self-test) --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct spinlock tl;
		struct time_test_ctx tc;
		struct parity_time_test_ops ttops;
		int r;

		ttops.read = time_test_read; ttops.ctx = &tc;
		ttops.slow_sleep_override = 0; ttops.slow_sleep_rc = 0;

		/* Test 2a: counter read failure mid-wait -> -EIO (not 0, not HW -ETIMEDOUT). */
		fake_mmio_open(&m, &f);
		tc.reads = 0; tc.fail_after = 1; tc.freq = 1000000u; tc.freq2 = 0; tc.freq_change_at = 0; tc.counter = 0;
		parity_wait_test_set(&ttops);
		r = parity_wait_reg(&m, 0x2000u, 0x1u, 0x1u, 2000u, 0u, 0);
		KCHECK(r == -EIO, "time: counter read failure becomes a time-base anomaly (-EIO)");

		/* Test 2b: frequency change mid-wait -> -EIO. */
		fake_mmio_open(&m, &f);
		tc.reads = 0; tc.fail_after = 0; tc.freq = 1000000u; tc.freq2 = 2000000u; tc.freq_change_at = 1; tc.counter = 0;
		parity_wait_test_set(&ttops);
		r = parity_wait_reg(&m, 0x2000u, 0x1u, 0x1u, 2000u, 0u, 0);
		KCHECK(r == -EIO, "time: frequency change mid-wait becomes a time-base anomaly (-EIO)");

		/* Test 3: udelay counter failure in reset -> reset stops with -EIO (no timeout retry). */
		fake_mmio_open(&m, &f);
		spin_init(&tl, LOCK_RANK_DEVICE, "ktest-uncore-b");
		tc.reads = 0; tc.fail_after = 3; tc.freq = 1000000u; tc.freq2 = 0; tc.freq_change_at = 0; tc.counter = 0;
		parity_wait_test_set(&ttops);
		r = parity_gt_reset_all(&tl, &m, 2000u);
		KCHECK(r == -EIO, "time: udelay failure aborts reset with -EIO (releases lock/forcewake, no retry)");

		/* Test 4: slow-stage wait API anomaly -> propagated as -EIO, not ignored. */
		fake_mmio_open(&m, &f);
		tc.reads = 0; tc.fail_after = 0; tc.freq = 1000000u; tc.freq2 = 0; tc.freq_change_at = 0; tc.counter = 0;
		ttops.slow_sleep_override = 1; ttops.slow_sleep_rc = EINVAL;
		parity_wait_test_set(&ttops);
		r = parity_wait_reg(&m, 0x2000u, 0x1u, 0x1u, 0u, 20u, 0);
		KCHECK(r == -EIO, "time: slow-wait API anomaly is propagated (-EIO), not ignored");

		parity_wait_test_set(0);   /* restore the real time source for any later use */
	}

	/* --- Power-well/domain operation bodies (unit C, GPU-free) --- */
	{
		static struct parity_power_domains pdc;
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct parity_pw_ctx cx;
		struct time_test_ctx tc;
		struct parity_time_test_ops ttops;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		unsigned k;
		int rc;

		osdep_trace_init(tr);
		parity_intel_power_domains_init(&pdc, 13u, -1, -1, tr);
		fake_mmio_open(&m, &f);
		cx.mmio = &m; cx.vga = 0; cx.irqs_enabled = 0;
		cx.vga_reset_calls = 0; cx.irq_post_enable_calls = 0; cx.ack_timeouts = 0;

		/* PW_A = well 4 (hsw ops, idx=5 -> REQ 0x2<<10, STATE 0x1<<10). */
		rc = parity_power_well_get(&pdc.power_wells[4], &cx);
		KCHECK(rc == 0 && pdc.power_wells[4].refcount == 1u && pdc.power_wells[4].hw_enabled == 1 &&
			(f.pw_hsw_req & (0x2u << 10)) != 0u,
			"pw: get enables the well (REQ set, ACK); refcount=1");
		rc = parity_power_well_get(&pdc.power_wells[4], &cx);
		KCHECK(rc == 0 && pdc.power_wells[4].refcount == 2u,
			"pw: nested get bumps refcount without re-enabling");
		parity_power_well_put(&pdc.power_wells[4], &cx);
		KCHECK(pdc.power_wells[4].refcount == 1u && (f.pw_hsw_req & (0x2u << 10)) != 0u,
			"pw: first put keeps the well enabled (refcount 2->1)");
		parity_power_well_put(&pdc.power_wells[4], &cx);
		KCHECK(pdc.power_wells[4].refcount == 0u && pdc.power_wells[4].hw_enabled == 0 &&
			(f.pw_hsw_req & (0x2u << 10)) == 0u,
			"pw: last put disables the well (REQ cleared)");

		/* Explicit enable() drives HW without touching the refcount. */
		pdc.power_wells[4].refcount = 0u;
		rc = parity_power_well_enable(&pdc.power_wells[4], &cx);
		KCHECK(rc == 0 && pdc.power_wells[4].refcount == 0u && pdc.power_wells[4].hw_enabled == 1,
			"pw: explicit enable() drives HW without touching the refcount");

		/* is_enabled: only REQ+STATE together are "enabled" (00/01/10/11). */
		fake_mmio_open(&m, &f);
		f.pw_hsw_req = 0u; f.pw_hsw_bios = 0u;
		KCHECK(parity_power_well_is_enabled(&pdc.power_wells[4], &cx) == 0,
			"pw: is_enabled 00 -> off");
		f.pw_hsw_bios = (0x2u << 10);   /* BIOS REQ -> STATE set, driver REQ clear */
		KCHECK(parity_power_well_is_enabled(&pdc.power_wells[4], &cx) == 0,
			"pw: is_enabled STATE-only (BIOS) -> driver-off");
		f.pw_hsw_bios = 0u; f.pw_hsw_req = (0x2u << 10);   /* driver REQ set */
		KCHECK(parity_power_well_is_enabled(&pdc.power_wells[4], &cx) == 1,
			"pw: is_enabled REQ+STATE -> on");

		/* sync_hw: take over a BIOS-held request (driver REQ set first, BIOS cleared). */
		fake_mmio_open(&m, &f);
		f.pw_hsw_bios = (0x2u << 10);   /* BIOS holds PW_A REQ */
		pdc.power_wells[4].hw_enabled = -1; pdc.power_wells[4].refcount = 0u;
		parity_power_well_sync_hw(&pdc.power_wells[4], &cx);
		KCHECK((f.pw_hsw_req & (0x2u << 10)) != 0u && (f.pw_hsw_bios & (0x2u << 10)) == 0u &&
			pdc.power_wells[4].hw_enabled == 1 && pdc.power_wells[4].refcount == 0u,
			"pw: sync_hw takes over the BIOS request (driver REQ set, BIOS cleared, refcount unchanged)");

		/* sync_hw with no BIOS request: just records is_enabled. */
		fake_mmio_open(&m, &f);
		f.pw_hsw_req = (0x2u << 10);   /* driver already requesting */
		pdc.power_wells[4].hw_enabled = -1;
		parity_power_well_sync_hw(&pdc.power_wells[4], &cx);
		KCHECK(pdc.power_wells[4].hw_enabled == 1,
			"pw: sync_hw records is_enabled when no BIOS handoff is needed");

		/* Two domains sharing PW_A (PIPE_A and PANEL_FITTER_A -> always_on + PW_A). */
		fake_mmio_open(&m, &f);
		for (k = 0u; k < pdc.num_power_wells; k++) {
			pdc.power_wells[k].refcount = 0u; pdc.power_wells[k].hw_enabled = -1;
		}
		rc = parity_display_power_get(&pdc, PARITY_PW_DOMAIN_PIPE_A, &cx);
		KCHECK(rc == 0 && pdc.power_wells[4].refcount == 1u,
			"pw: display_power_get(PIPE_A) takes always_on + PW_A in order");
		rc = parity_display_power_get(&pdc, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_A, &cx);
		KCHECK(rc == 0 && pdc.power_wells[4].refcount == 2u,
			"pw: a second domain sharing PW_A bumps its refcount to 2");
		parity_display_power_put(&pdc, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_A, &cx);
		KCHECK(pdc.power_wells[4].refcount == 1u, "pw: putting one sharer keeps PW_A enabled");
		parity_display_power_put(&pdc, PARITY_PW_DOMAIN_PIPE_A, &cx);
		KCHECK(pdc.power_wells[4].refcount == 0u, "pw: putting the last sharer disables PW_A");

		/* disable tolerates another requester (BIOS): STATE stays; no wait hang. */
		fake_mmio_open(&m, &f);
		f.pw_hsw_bios = (0x2u << 10);   /* BIOS holds PW_A */
		pdc.power_wells[4].refcount = 0u; pdc.power_wells[4].hw_enabled = -1;
		(void)parity_power_well_get(&pdc.power_wells[4], &cx);
		parity_power_well_put(&pdc.power_wells[4], &cx);
		KCHECK((osdep_mmio_raw_read32(&m, 0x45404u) & (0x1u << 10)) != 0u &&
			pdc.power_wells[4].hw_enabled == 0,
			"pw: disable tolerates a BIOS-held well (STATE stays; driver ownership off)");

		/* post-enable: PW_2 (has_vga) calls reset_io_mem; PW_A IRQ gated off before P4. */
		fake_mmio_open(&m, &f);
		cx.vga_reset_calls = 0; cx.irq_post_enable_calls = 0; cx.irqs_enabled = 0;
		pdc.power_wells[3].refcount = 0u; pdc.power_wells[3].hw_enabled = -1;   /* PW_2 */
		(void)parity_power_well_enable(&pdc.power_wells[3], &cx);
		KCHECK(cx.vga_reset_calls == 1u,
			"pw: PW_2 post-enable calls intel_vga_reset_io_mem (has_vga)");
		cx.vga_reset_calls = 0;
		pdc.power_wells[4].refcount = 0u; pdc.power_wells[4].hw_enabled = -1;   /* PW_A */
		(void)parity_power_well_enable(&pdc.power_wells[4], &cx);
		KCHECK(cx.vga_reset_calls == 0u && cx.irq_post_enable_calls == 0u,
			"pw: PW_A post-enable: no VGA; pipe-IRQ post-enable gated off before P4");

		/* ACK timeout: void ops warn + continue (not a caller failure); refcount still taken. */
		fake_mmio_open(&m, &f);
		f.pw_no_ack = 1;
		cx.ack_timeouts = 0;
		pdc.power_wells[4].refcount = 0u; pdc.power_wells[4].hw_enabled = -1;
		rc = parity_power_well_enable(&pdc.power_wells[4], &cx);
		KCHECK(rc == 0 && cx.ack_timeouts == 1u,
			"pw: a missing HW ACK is warned and the enable continues (not a caller failure)");

		/* Time-base anomaly during the ACK wait: -EIO (distinct from a HW timeout). */
		fake_mmio_open(&m, &f);
		tc.reads = 0; tc.fail_after = 1; tc.freq = 1000000u; tc.freq2 = 0;
		tc.freq_change_at = 0; tc.counter = 0;
		ttops.read = time_test_read; ttops.ctx = &tc;
		ttops.slow_sleep_override = 0; ttops.slow_sleep_rc = 0;
		parity_wait_test_set(&ttops);
		pdc.power_wells[4].refcount = 0u; pdc.power_wells[4].hw_enabled = -1;
		rc = parity_power_well_enable(&pdc.power_wells[4], &cx);
		KCHECK(rc == -EIO,
			"pw: a time-base anomaly during the ACK wait is -EIO (distinct from a HW timeout)");
		parity_wait_test_set(0);
	}

	/* --- Common PCODE skl_pcode_request (D1, GPU-free scripted) --- */
	{
		static const uint32_t sc1[][3] = { { 0x1u, 0u, 0x0u } };
		static const uint32_t sc2[][3] = { { 0x0u, 0u, 0x0u }, { 0x0u, 0u, 0x0u }, { 0x1u, 0u, 0x0u } };
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct mutex sb;
		struct time_test_ctx tc;
		struct parity_time_test_ops ttops;
		int r;

		(void)mutex_init(&sb, LOCK_RANK_DEVICE, "ktest-sb");

		/* First request already satisfies the reply -> no re-request. */
		fake_mmio_open(&m, &f);
		f.script = sc1; f.script_len = 1u;
		r = parity_skl_pcode_request(&sb, &m, 0x7u, 0u, 0x1u, 0x1u, 100);
		KCHECK(r == 0 && f.txn == 1u,
			"pcode: skl_pcode_request first request satisfies reply (no re-request)");

		/* Reply satisfied after a few requests -> each sends a fresh request. */
		fake_mmio_open(&m, &f);
		f.script = sc2; f.script_len = 3u;
		r = parity_skl_pcode_request(&sb, &m, 0x7u, 0u, 0x1u, 0x1u, 100);
		KCHECK(r == 0 && f.txn == 3u,
			"pcode: skl_pcode_request re-requests until the reply matches");

		/* Persistent PCODE error status -> propagated (not masked as -ETIMEDOUT/-EIO). */
		fake_mmio_open(&m, &f);
		f.pcode_sticky_status = 0x1;   /* GEN6_PCODE_ILLEGAL_CMD -> -ENXIO */
		r = parity_skl_pcode_request(&sb, &m, 0x7u, 0u, 0x1u, 0x1u, 1);
		KCHECK(r != 0 && r != -EIO && r != -ETIMEDOUT,
			"pcode: a persistent PCODE error status is returned (not a timeout/anomaly)");

		/* Time-base anomaly during the request -> -EIO, lock released, no busy retry. */
		fake_mmio_open(&m, &f);
		f.pcode_no_ready = 1;   /* READY never clears -> the wait polls into the fault */
		tc.reads = 0; tc.fail_after = 1; tc.freq = 1000000u; tc.freq2 = 0;
		tc.freq_change_at = 0; tc.counter = 0;
		ttops.read = time_test_read; ttops.ctx = &tc;
		ttops.slow_sleep_override = 0; ttops.slow_sleep_rc = 0;
		parity_wait_test_set(&ttops);
		r = parity_skl_pcode_request(&sb, &m, 0x7u, 0u, 0x1u, 0x1u, 100);
		parity_wait_test_set(0);
		KCHECK(r == -EIO,
			"pcode: a time-base anomaly stops with -EIO (not a normal PCODE timeout)");
	}

	/* --- combo PHY init (D2, GPU-free) --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		int r;
		unsigned n;

		osdep_trace_init(tr);

		/* Init needed: a fresh PHY is not COMP_INIT-enabled, so verify fails. */
		fake_mmio_open(&m, &f);
		KCHECK(parity_combo_phy_verify_state(&m, PARITY_COMBO_PHY_A) == 0,
			"combo: verify fails when the PHY is not COMP_INIT-enabled");
		parity_combo_phy_init_one(&m, PARITY_COMBO_PHY_A);
		KCHECK((osdep_mmio_raw_read32(&m, 0x162100u) & (1u << 31)) != 0u &&   /* COMP_DW0 COMP_INIT */
			(osdep_mmio_raw_read32(&m, 0x162120u) & (1u << 24)) != 0u &&   /* COMP_DW8 IREFGEN (master A) */
			(osdep_mmio_raw_read32(&m, 0x162014u) & (1u << 4)) != 0u &&    /* CL_DW5 CL_POWER_DOWN */
			osdep_mmio_raw_read32(&m, 0x162124u) == 0x62AB67BBu,           /* COMP_DW9 procmon[0] */
			"combo: init_one programs COMP_INIT/IREFGEN/CL_POWER_DOWN/procmon");
		KCHECK(parity_combo_phy_verify_state(&m, PARITY_COMBO_PHY_A) == 1,
			"combo: after init_one the PHY verifies (GRP->LN broadcast reflected)");

		/* COMP_INIT set but procmon mismatch -> NOT treated as initialised. */
		fake_mmio_open(&m, &f);
		osdep_mmio_raw_write32(&m, 0x162100u, (1u << 31));   /* COMP_DW0 COMP_INIT only */
		KCHECK(parity_combo_phy_verify_state(&m, PARITY_COMBO_PHY_A) == 0,
			"combo: COMP_INIT alone (procmon/other mismatch) is not verified");

		/* All-appropriate: both PHYs already correct -> top-level re-inits none. */
		fake_mmio_open(&m, &f);
		parity_combo_phy_init_one(&m, PARITY_COMBO_PHY_A);
		parity_combo_phy_init_one(&m, PARITY_COMBO_PHY_B);
		n = 99u;
		r = parity_intel_combo_phy_init(&m, tr, &n);
		KCHECK(r == 0 && n == 0u,
			"combo: init returns 0 (success) and re-initialises none when all verify");

		/* Unconfigured: top-level programs both combo PHYs. */
		fake_mmio_open(&m, &f);
		n = 99u;
		r = parity_intel_combo_phy_init(&m, tr, &n);
		KCHECK(r == 0 && n == 2u,
			"combo: init returns 0 (NOT the count) so the D3 parent proceeds; 2 programmed");
	}

	/* --- CDCLK init (D2, GPU-free): hook / readout / sanitize / prepare-fail --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct mutex tsb;
		struct parity_cdclk_dev cd;
		struct parity_cdclk_config cfg;
		unsigned i;

		(void)mutex_init(&tsb, LOCK_RANK_DEVICE, "ktest-cdclk-sb");

		/* Hook selection: ADL-P rev 0x0c -> STEP_D0 -> adlp table + tgl funcs. */
		KCHECK(parity_adlp_display_step(0x0Cu) == PARITY_STEP_D0,
			"cdclk: adlp revid 0x0c maps to display STEP_D0 (not raw revision)");
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, parity_adlp_display_step(0x0Cu), 1);
		KCHECK(cd.table == parity_adlp_cdclk_table() &&
			cd.funcs == PARITY_CDCLK_FUNCS_TGL &&
			cd.has_cdclk_crawl == 1 && cd.has_cdclk_squash == 0,
			"cdclk: hooks select adlp_cdclk_table + tgl funcs, crawl=1 squash=0");

		/* Readout: 38.4MHz refclk, PLL ratio 34 (locked), CD2X div /2. */
		fake_mmio_open(&m, &f);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));       /* SKL_DSSM 38.4MHz */
		osdep_mmio_raw_write32(&m, 0x46070u, (1u << 31) | 34u); /* DE PLL enable, ratio 34 -> +LOCK */
		osdep_mmio_raw_write32(&m, 0x46000u, 0x518u);          /* CDCLK_CTL: decimal(652800), div/2 */
		for (i = 0u; i < sizeof(cfg); i++) ((char *)&cfg)[i] = 0;
		parity_bxt_get_cdclk(&cd, &cfg);
		KCHECK(cfg.ref == 38400u && cfg.vco == 1305600u && cfg.bypass == 19200u &&
			cfg.cdclk == 652800u && cfg.voltage_level == 3u,
			"cdclk: bxt_get_cdclk decodes ref/vco/bypass/cdclk/voltage (652800 @ 38.4MHz)");

		/* CD-1 no-change: a legal pre-OS state -> init_hw reprograms nothing. */
		cd.hw = cfg;   /* sanitize re-reads anyway */
		parity_intel_cdclk_init_hw(&cd);
		KCHECK(cd.diag_no_change == 1 && cd.hw.cdclk == 652800u && cd.hw.vco == 1305600u &&
			cd.diag_hw_sequence_reached == 0,
			"cdclk: CD-1 legal state -> no reprogram (cdclk/vco kept, no HW writes)");

		/* CD-2 sanitize-needed: PLL disabled -> force full re-setup (cdclk=0, vco=~0). */
		fake_mmio_open(&m, &f);
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));   /* 38.4MHz */
		osdep_mmio_raw_write32(&m, 0x46070u, 0u);           /* DE PLL disabled */
		parity_bxt_sanitize_cdclk(&cd);
		KCHECK(cd.hw.cdclk == 0u && cd.hw.vco == ~0u,
			"cdclk: CD-2 pre-OS PLL off -> sanitize forces cdclk=0, vco=~0 (not a real freq)");

		/* CD-3 prepare-fail: PCU rejects PREPARE -> no PLL / CDCLK_CTL writes. */
		fake_mmio_open(&m, &f);
		f.pcode_sticky_status = 0x2;   /* every PCODE txn returns an error status */
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));
		cd.hw.ref = 38400u; cd.hw.bypass = 19200u; cd.hw.vco = ~0u; cd.hw.cdclk = 0u;
		for (i = 0u; i < sizeof(cfg); i++) ((char *)&cfg)[i] = 0;
		cfg.cdclk = 652800u; cfg.vco = 1305600u; cfg.voltage_level = 3u;
		parity_bxt_set_cdclk(&cd, &cfg);
		KCHECK(cd.diag_prepare_status != 0 && cd.diag_hw_sequence_reached == 0 &&
			osdep_mmio_raw_read32(&m, 0x46000u) == 0u,
			"cdclk: CD-3 PCU prepare failure -> no PLL/CDCLK_CTL writes");
	}

	/* --- CDCLK change path (D2, GPU-free): CD-2a full re-setup / CD-2b crawl / CD-4 notify-fail --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct mutex tsb;
		struct parity_cdclk_dev cd;
		struct parity_cdclk_config cfg;
		unsigned i;
		int pll_dis, pll_en, ctl, freq_req;
		/* PREPARE approves (reply READY), notify succeeds. */
		static const struct fake_pcode_txn ok_179k[] = {
			{ 0x7u, 0x3u, 0x1u, 0u, 0x0u, 0u },   /* PREPARE: reply READY, status ok */
			{ 0x7u, 0x0u, 0u,   0u, 0x0u, 0u },   /* notify voltage_level 0, status ok */
		};
		/* CD-4: crawl (requested voltage 2) but the FINAL notify is rejected (0x11 -> -EACCES). */
		static const struct fake_pcode_txn notifyfail_557k[] = {
			{ 0x7u, 0x3u, 0x1u, 0u, 0x00u, 0u },  /* PREPARE ok */
			{ 0x7u, 0x2u, 0u,   0u, 0x11u, 0u },  /* notify voltage 2 REJECTED */
		};
		/* Crawl fixture: PREPARE ok, notify voltage_level 2 ok. */
		static const struct fake_pcode_txn ok_557k[] = {
			{ 0x7u, 0x3u, 0x1u, 0u, 0x0u, 0u },
			{ 0x7u, 0x2u, 0u,   0u, 0x0u, 0u },
		};

		(void)mutex_init(&tsb, LOCK_RANK_DEVICE, "ktest-cdclk-chg");

		/* ---- CD-2a: init_hw() drives a full re-setup to completion ---- */
		fake_mmio_open(&m, &f);
		f.ptxn = ok_179k; f.ptxn_len = 2u; f.ptxn_i = 0u;
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));   /* 38.4MHz */
		osdep_mmio_raw_write32(&m, 0x46070u, 0u);           /* DE PLL disabled -> sanitize forces re-setup */
		f.wt_n = 0u;                                        /* trace only the init_hw writes */
		parity_intel_cdclk_init_hw(&cd);
		pll_dis = fake_wt_find(&f, 0x46070u, 0u, (1u << 31));           /* a write with PLL_ENABLE clear */
		pll_en  = fake_wt_find(&f, 0x46070u, (1u << 31) | 14u, (1u << 31) | 0xffu);
		ctl     = fake_wt_find(&f, 0x46000u, 0x780164u, 0xffffffffu);   /* decimal(179200)|div1.5|PIPE_NONE */
		KCHECK(f.ptxn_i == 2u && f.ptxn_bad == 0 &&
			pll_dis >= 0 && pll_en >= 0 && ctl >= 0 && pll_en < ctl &&
			cd.diag_hw_sequence_reached == 1 && cd.diag_prepare_status == 0 &&
			cd.diag_notify_status == 0 &&
			cd.hw.cdclk == 179200u && cd.hw.vco == 537600u &&
			cd.hw.voltage_level == 0u,
			"cdclk: CD-2a init_hw full re-setup (PREPARE->PLL disable+enable->CDCLK_CTL->notify->state)");

		/* ---- CD-2a variant: PLL locked but CDCLK_CTL bad -> still full re-setup ---- */
		fake_mmio_open(&m, &f);
		f.ptxn = ok_179k; f.ptxn_len = 2u; f.ptxn_i = 0u;
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));
		osdep_mmio_raw_write32(&m, 0x46070u, (1u << 31) | 34u);   /* locked ratio 34 (vco 1305600) */
		osdep_mmio_raw_write32(&m, 0x46000u, 0u);                 /* CDCLK_CTL decimal mismatch */
		f.wt_n = 0u;
		parity_intel_cdclk_init_hw(&cd);
		KCHECK(f.ptxn_i == 2u && f.ptxn_bad == 0 && cd.diag_no_change == 0 &&
			cd.hw.cdclk == 179200u && cd.hw.vco == 537600u,
			"cdclk: CD-2a variant (PLL locked, CDCLK_CTL bad) still forces full re-setup via unknown");

		/* ---- CD-2b: setter drives a crawl from a known-good state (no PLL disable) ---- */
		fake_mmio_open(&m, &f);
		f.ptxn = ok_557k; f.ptxn_len = 2u; f.ptxn_i = 0u;
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));
		cd.hw.ref = 38400u; cd.hw.bypass = 19200u; cd.hw.cdclk = 307200u; cd.hw.vco = 614400u;
		for (i = 0u; i < sizeof(cfg); i++) ((char *)&cfg)[i] = 0;
		cfg.cdclk = 556800u; cfg.vco = 1113600u; cfg.voltage_level = 2u;
		f.wt_n = 0u;
		parity_bxt_set_cdclk(&cd, &cfg);
		pll_dis  = fake_wt_find(&f, 0x46070u, 0u, (1u << 31));            /* must be absent (no disable) */
		freq_req = fake_wt_find(&f, 0x46070u, (1u << 23), (1u << 23));    /* crawl FREQ_REQ */
		ctl      = fake_wt_find(&f, 0x46000u, 0x380458u, 0xffffffffu);    /* decimal(556800)|div1|PIPE_NONE */
		KCHECK(f.ptxn_i == 2u && f.ptxn_bad == 0 &&
			pll_dis < 0 && freq_req >= 0 && ctl >= 0 &&
			cd.hw.cdclk == 556800u && cd.hw.vco == 1113600u && cd.hw.voltage_level == 2u,
			"cdclk: CD-2b crawl (ratio+FREQ_REQ, LOCK+ACK, no PLL disable) from known-good state");

		/* ---- CD-2b guard: an identical VCO must not emit a crawl request ---- */
		fake_mmio_open(&m, &f);
		f.ptxn = ok_557k; f.ptxn_len = 2u; f.ptxn_i = 0u;
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));
		cd.hw.ref = 38400u; cd.hw.bypass = 19200u; cd.hw.cdclk = 556800u; cd.hw.vco = 1113600u;
		for (i = 0u; i < sizeof(cfg); i++) ((char *)&cfg)[i] = 0;
		cfg.cdclk = 556800u; cfg.vco = 1113600u; cfg.voltage_level = 2u;
		f.wt_n = 0u;
		parity_bxt_set_cdclk(&cd, &cfg);
		KCHECK(fake_wt_find(&f, 0x46070u, 0u, 0u) < 0,
			"cdclk: CD-2b guard: same VCO emits no PLL/crawl write");

		/* ---- CD-4: crawl setter (requested voltage 2), final notify rejected ---- */
		fake_mmio_open(&m, &f);
		f.ptxn = notifyfail_557k; f.ptxn_len = 2u; f.ptxn_i = 0u;
		for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;
		parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);
		cd.m = &m; cd.sb_lock = &tsb;
		osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));
		cd.hw.ref = 38400u; cd.hw.bypass = 19200u;
		cd.hw.cdclk = 307200u; cd.hw.vco = 614400u; cd.hw.voltage_level = 0u;  /* pre-change */
		for (i = 0u; i < sizeof(cfg); i++) ((char *)&cfg)[i] = 0;
		cfg.cdclk = 556800u; cfg.vco = 1113600u; cfg.voltage_level = 2u;       /* requested */
		f.wt_n = 0u;
		parity_bxt_set_cdclk(&cd, &cfg);
		freq_req = fake_wt_find(&f, 0x46070u, (1u << 23), (1u << 23));   /* crawl happened */
		pll_dis  = fake_wt_find(&f, 0x46070u, 0u, (1u << 31));          /* must be absent */
		ctl      = fake_wt_find(&f, 0x46000u, 0x380458u, 0xffffffffu);  /* CDCLK_CTL written */
		/*
		 * Partial update (NOT memcmp==0): the PLL crawl helper advanced hw.vco, and
		 * CDCLK_CTL was written, but the final intel_update_cdclk() must NOT run, so
		 * hw.cdclk keeps its pre-change value (307200, not 556800) and voltage_level
		 * is NOT overwritten with the requested value 2 -- it stays the reference's
		 * retained pre-change value 0.
		 */
		KCHECK(cd.diag_prepare_status == 0 && cd.diag_hw_sequence_reached == 1 &&
			cd.diag_notify_status == -EACCES &&      /* raw status 0x11 -> -EACCES */
			freq_req >= 0 && pll_dis < 0 && ctl >= 0 &&
			cd.hw.vco == 1113600u &&                 /* crawl helper advanced VCO */
			cd.hw.cdclk == 307200u &&                /* update_cdclk did NOT run (not 556800) */
			cd.hw.voltage_level == 0u,               /* requested voltage 2 NOT copied in */
			"cdclk: CD-4 notify-fail leaves a partial state (VCO advanced, cdclk/voltage kept, not requested)");
	}

	/* --- D3 integration: intel_power_domains_init_hw(false) end-to-end (GPU-free) --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct mutex tsb;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		struct parity_power_domains pd;
		struct parity_cdclk_dev cd;
		struct parity_pw_ctx pwc;
		struct parity_display_core dc;
		int pw1;
		unsigned i;
		static const struct fake_pcode_txn ok_cdclk[] = {
			{ 0x7u, 0x3u, 0x1u, 0u, 0x0u, 0u },   /* PREPARE reply READY */
			{ 0x7u, 0x0u, 0u,   0u, 0x0u, 0u },   /* notify voltage 0 */
		};
		static const struct parity_time_test_ops fault_ops = { ktest_fail_read, 0, 0, 0 };

		(void)mutex_init(&tsb, LOCK_RANK_DEVICE, "ktest-d3-sb");

		/* Shared device state builder. */
		#define D3_SETUP() do {                                                   \
			osdep_trace_init(tr);                                             \
			fake_mmio_open(&m, &f);                                            \
			(void)parity_intel_power_domains_init(&pd, 13u, -1, 1, tr);       \
			for (i = 0u; i < sizeof(cd); i++) ((char *)&cd)[i] = 0;            \
			parity_intel_init_cdclk_hooks(&cd, 13, PARITY_STEP_D0, 1);         \
			cd.m = &m; cd.sb_lock = &tsb;                                      \
			for (i = 0u; i < sizeof(pwc); i++) ((char *)&pwc)[i] = 0;          \
			pwc.mmio = &m;                                                     \
			for (i = 0u; i < sizeof(dc); i++) ((char *)&dc)[i] = 0;            \
			dc.pd = &pd; dc.cd = &cd; dc.pwc = &pwc; dc.m = &m; dc.sb_lock = &tsb; \
			dc.dram_type = PARITY_DRAM_LPDDR5; dc.dram_channels = 2u;          \
			osdep_mmio_raw_write32(&m, 0x51004u, (2u << 29));  /* 38.4MHz */   \
			parity_wait_test_reset_fault();   /* clear latch from earlier cases */ \
			pwc.cd = &cd; pwc.dbuf_slices = &dc.dbuf_enabled_slices;                \
			pwc.target_dc_state = pd.target_dc_state;                              \
			pwc.allowed_dc_mask = pd.allowed_dc_mask;                              \
			parity_vga_io_test_set(&vga_rec_ops);                                  \
			g_vga_rec.n = 0u; g_vga_rec.got = 0; g_vga_rec.put = 0;                \
		} while (0)

		pw1 = 0;

		/* ---- D-NORMAL: PHY/CDCLK need init; PCODE/well/DBUF respond normally ---- */
		D3_SETUP();
		f.fuse_status = 0xFFFFFFFFu;              /* all PG fuses already distributed */
		f.ptxn = ok_cdclk; f.ptxn_len = 2u; f.ptxn_i = 0u;
		osdep_mmio_raw_write32(&m, 0x46070u, 0u); /* DE PLL off -> CDCLK full re-setup */
		parity_intel_power_domains_init_hw(&dc, 0);
		pw1 = parity_power_well_by_id(&pd, PARITY_SKL_DISP_PW_1);
		KCHECK(dc.fault_stop == 0 && dc.reached_init_ref == 1 && dc.reached_sync_hw == 1 &&
			dc.initializing == 0 && dc.init_wakeref_held == 1,
			"d3: D-NORMAL parent completes -> INIT ref held + all-well sync reached");
		KCHECK(f.ptxn_bad == 0 && cd.hw.cdclk == 179200u,
			"d3: D-NORMAL CDCLK ran on the shared device (cdclk 179200, PCODE consumed)");
		KCHECK(dc.dbuf_enabled_slices == 0x1u &&
			osdep_mmio_raw_read32(&m, 0x45134u) == 0x1Cu,
			"d3: D-NORMAL DBUF slice S1 enabled + BW_BUDDY page mask programmed");
		KCHECK((osdep_mmio_raw_read32(&m, 0x162100u) & (1u << 31)) != 0u &&
			pw1 >= 0 && pd.power_wells[pw1].hw_enabled == 1,
			"d3: D-NORMAL combo PHY_A initialised + PW1 enabled (same MMIO)");
		KCHECK(pwc.dc_off_enable_calls > 0u && pwc.dc_off_cdclk_readouts > 0u &&
			pwc.dc_off_dbuf_asserts > 0u && pwc.dc_off_combo_inits > 0u,
			"d3: D-NORMAL INIT get enters DC_off body (DC disable + CDCLK/DBUF compare + PHY)");
		KCHECK(g_vga_rec.got == 1 && g_vga_rec.put == 1 && g_vga_rec.n >= 4u &&
			g_vga_rec.seq[0] == 1 && g_vga_rec.seq[1] == 2 &&
			g_vga_rec.seq[2] == 3 && g_vga_rec.seq[3] == 4 &&
			g_vga_rec.last_written == g_vga_rec.last_read && g_vga_rec.last_read == 0xABu,
			"d3: D-NORMAL VGA reset owns LEGACY_IO: get -> MIS_R read -> MIS_W write -> put");

		/* ---- D-REMOVE: from D-NORMAL complete, diagnostic driver-remove ---- */
		{
			unsigned k, sum_before = 0u, sum_after = 0u;

			for (k = 0u; k < pd.num_power_wells; k++)
				sum_before += pd.power_wells[k].refcount;   /* INIT get bumped its domain wells */
			parity_intel_power_domains_driver_remove(&dc);
			for (k = 0u; k < pd.num_power_wells; k++)
				sum_after += pd.power_wells[k].refcount;
			KCHECK(dc.pm_wakeref == 0 && dc.init_wakeref_held == 0 &&
				sum_before > 0u && sum_after == sum_before,     /* no domain put: wells stay enabled */
				"d3: D-REMOVE cancels the rpm wakeref only; well refcounts unchanged (no domain put)");
		}

		/* ---- D-PRESERVE: PHY/CDCLK already appropriate; DBUF already has S1 ---- */
		D3_SETUP();
		f.fuse_status = 0xFFFFFFFFu;
		f.ptxn = ok_cdclk; f.ptxn_len = 0u; f.ptxn_i = 0u;   /* len 0: any PCODE txn fails the test */
		/* Pre-program a legal CDCLK state (locked ratio 34 @ 38.4 -> 652800, div/2). */
		osdep_mmio_raw_write32(&m, 0x46070u, (1u << 31) | 34u);
		osdep_mmio_raw_write32(&m, 0x46000u, 0x518u);
		/* Pre-initialise both combo PHYs so verify passes (no re-init). */
		parity_combo_phy_init_one(&m, PARITY_COMBO_PHY_A);
		parity_combo_phy_init_one(&m, PARITY_COMBO_PHY_B);
		/* DBUF already has slices S1 AND S2 powered (distinguish "kept" from "always S1"). */
		osdep_mmio_raw_write32(&m, 0x44FE8u, (1u << 31));   /* S1 */
		osdep_mmio_raw_write32(&m, 0x44300u, (1u << 31));   /* S2 */
		parity_intel_power_domains_init_hw(&dc, 0);
		KCHECK(dc.fault_stop == 0 && dc.reached_init_ref == 1 && dc.reached_sync_hw == 1 &&
			f.ptxn_bad == 0 && f.ptxn_i == 0u &&           /* NO PCODE (no re-setup) */
			cd.diag_no_change == 1 &&                      /* CDCLK left the HW as-is */
			dc.dbuf_enabled_slices == 0x3u,                /* S1+S2 both kept (not forced to S1) */
			"d3: D-PRESERVE keeps existing DBUF slices (S1+S2) + appropriate PHY/CDCLK (no PCODE)");

		/* ---- D-FAULT: inject a time/accessor anomaly mid-child ---- */
		D3_SETUP();
		f.fuse_status = 0xFFFFFFFFu;
		f.ptxn = ok_cdclk; f.ptxn_len = 2u; f.ptxn_i = 0u;
		osdep_mmio_raw_write32(&m, 0x46070u, 0u);
		parity_wait_test_set(&fault_ops);          /* every real-time read now faults */
		parity_intel_power_domains_init_hw(&dc, 0);
		parity_wait_test_set(0);                    /* restore the real time source */
		KCHECK(dc.fault_stop == 1 && dc.fault_where != 0 &&
			dc.reached_init_ref == 0 && dc.reached_sync_hw == 0 &&
			dc.init_wakeref_held == 0 && dc.initializing == 0,
			"d3: D-FAULT stops at the first anomaly, takes no INIT ref, runs no sync");

		/* driver-remove on a faulted (no INIT ref) state must release nothing. */
		dc.pm_wakeref = 0;
		parity_intel_power_domains_driver_remove(&dc);
		KCHECK(dc.pm_wakeref == 0 && dc.init_wakeref_held == 0,
			"d3: D-FAULT driver-remove releases no un-acquired INIT reference");

		parity_vga_io_test_set(0);
		#undef D3_SETUP
	}

	/* --- kern_usleep_range: real yielding sleep-range (GPU-free) --- */
	{
		uint64_t n0 = 0, n1 = 0, freq = 0, f1 = 0, t0, t1, elapsed_us = 0;
		unsigned long c0, c1;

		g_corun_counter = 0u;
		g_corun_stop = 0;
		if (spawn_detached(sleep_corunner, 0) == 0) {
			kern_usleep_range(2000u, 3000u);         /* let the co-runner start */
			c0 = g_corun_counter;
			t0 = sched_ticks();
			(void)kern_rtc_read_counter(&n0, &freq);
			kern_usleep_range(20000u, 25000u);       /* the sleep under test */
			(void)kern_rtc_read_counter(&n1, &f1);
			t1 = sched_ticks();
			c1 = g_corun_counter;
			g_corun_stop = 1;
			if (freq != 0u && f1 == freq)
				elapsed_us = (n1 - n0) * 1000000u / freq;
			/*
			 * The absolute deadline held (elapsed >= min, allowing a little
			 * slack), the sleep crossed >= 1 scheduler tick (it yielded rather
			 * than busy-spinning a sub-tick), and the co-runner made progress
			 * while we slept.
			 */
			KCHECK(elapsed_us >= 19000u && (t1 - t0) >= 1u && c1 > c0,
				"usleep_range: 20-25ms yields the CPU (>=19ms elapsed, crossed a tick, co-runner ran)");
		} else {
			KCHECK(0, "usleep_range: could not spawn the co-runner");
		}
	}

	/* --- real preemption: reschedule from a real IRQ is deferred until the outermost enable --- */
	if (hal_cpu_count() > PT_CPU) {
		waitq_init(&g_pt_wq, "pt");
		spin_init(&g_pt_lk, LOCK_RANK_USYNC, "pt");
		g_pt_b_ran = 0; g_pt_b_sleeping = 0; g_pt_fired = 0;
		g_pt_after_fire = -1; g_pt_after_inner = -1; g_pt_after_outer = -1;
		g_pt_ok = 0; g_pt_done = 0;

		if (spawn_on_cpu(pt_thread_b, 0, PT_CPU) == 0 &&
		    spawn_on_cpu(pt_thread_a, 0, PT_CPU) == 0) {
			uint64_t b = 0, f = 0, now = 0, nf = 0, target;
			(void)kern_rtc_read_counter(&b, &f);
			target = (f != 0u) ? f : 0u;   /* ~1s bound for A to finish */
			while (g_pt_ok == 0) {
				if (!kern_rtc_read_counter(&now, &nf) || (target != 0u && now - b >= target))
					break;
			}
			kern_diag_oneshot_disarm();
			KCHECK(g_pt_ok == 1 && g_pt_fired == 1 &&
				g_pt_after_fire == 0 &&      /* IRQ reschedule request: B deferred */
				g_pt_after_inner == 0 &&     /* inner enable: still deferred (count 1) */
				g_pt_after_outer == 1,       /* outermost enable: switch happens, B ran */
				"preempt: real IRQ reschedule is deferred while preempt-disabled, runs at outermost enable");
		} else {
			KCHECK(0, "preempt: could not spawn the A/B threads");
		}
	}

	/* --- PCODE additional (preempt-off) region: approval + time-base fault exits --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct mutex sb;
		int ret;

		/* (a) normal region never approves; the additional preempt-off region does. */
		(void)mutex_init(&sb, LOCK_RANK_DEVICE, "pcode-add-a");
		fake_mmio_open(&m, &f);
		f.pcode_approve_when_preempt = 1;
		parity_wait_test_set(&pcode_ok_ops);
		ret = parity_skl_pcode_request(&sb, &m, 0x7u, 0x3u, 0x1u, 0x1u, 1);
		parity_wait_test_set(0);
		KCHECK(ret == 0 && f.pcode_txn_count >= 3u &&
			sched_test_preempt_count() == 0u &&   /* preempt restored */
			mutex_trylock(&sb),                   /* mutex released */
			"pcode: normal region unapproved -> additional (preempt-off) region approves; preempt/mutex restored");
		mutex_unlock(&sb);

		/* (b) counter read faults inside the additional region -> -EIO (not a timeout). */
		g_pcb_preempt_reads = 0u;
		(void)mutex_init(&sb, LOCK_RANK_DEVICE, "pcode-add-b");
		fake_mmio_open(&m, &f);
		f.pcode_reply_deny = 1;
		parity_wait_test_set(&pcode_fault_ops);
		ret = parity_skl_pcode_request(&sb, &m, 0x7u, 0x3u, 0x1u, 0x1u, 1);
		parity_wait_test_set(0);
		{
			int tl = mutex_trylock(&sb);
			KCHECK(ret == -EIO &&                        /* time-base anomaly, NOT -ETIMEDOUT */
				sched_test_preempt_count() == 0u &&   /* preempt restored */
				tl,                                   /* mutex released */
				"pcode: additional-region counter fault -> -EIO (not a timeout); preempt/mutex restored");
			if (tl) mutex_unlock(&sb);
		}
	}

	/* --- A: pure one-shot next-event / tick-update calculation (fake clock) --- */
	{
		struct parity_timer_calc tc;

		parity_timer_calc_init(&tc, 0u, 1000u);   /* period = 1000 counter units */
		KCHECK(parity_timer_next_event(&tc) == 1000u,
			"timer-calc: the first event is the next logical tick");
		parity_timer_set_sleep(&tc, 200u);
		KCHECK(parity_timer_next_event(&tc) == 200u,
			"timer-calc: an earlier sleep deadline (2ms) wins over the 10ms tick");
		KCHECK(parity_timer_on_fire(&tc, 200u) == 0u && tc.next_tick == 1000u,
			"timer-calc: a sleep-only fire delivers 0 ticks and does not move the tick");
		parity_timer_clear_sleep(&tc);
		KCHECK(parity_timer_next_event(&tc) == 1000u,
			"timer-calc: cancelling the earliest sleep re-selects the tick");
		KCHECK(parity_timer_on_fire(&tc, 1000u) == 1u && tc.next_tick == 2000u,
			"timer-calc: a tick fire delivers exactly 1 tick and advances the deadline");
		parity_timer_set_sleep(&tc, 1500u);
		parity_timer_set_sleep(&tc, 1200u);
		parity_timer_set_sleep(&tc, 1800u);
		KCHECK(parity_timer_next_event(&tc) == 1200u,
			"timer-calc: set_sleep keeps the earliest of several deadlines");
		{
			struct parity_timer_calc t2;

			parity_timer_calc_init(&t2, 0u, 1000u);
			KCHECK(parity_timer_on_fire(&t2, 2500u) == 2u && t2.next_tick == 3000u,
				"timer-calc: a late fire delivers every crossed tick, deadline moves past now (no wait on past)");
			KCHECK(parity_timer_on_fire(&t2, 2999u) == 0u,
				"timer-calc: no whole tick elapsed -> 0 delivered (a sub-tick fire is not a tick)");
		}
	}

	/* --- B: fuse arrives late -> the slow poll observes it; never -> warn+continue --- */
	{
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct parity_power_domains pd;
		struct parity_pw_ctx pwc;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		int pw1, rc;
		unsigned i;

		osdep_trace_init(tr);
		fake_mmio_open(&m, &f);
		(void)parity_intel_power_domains_init(&pd, 13u, -1, 1, tr);
		for (i = 0u; i < sizeof(pwc); i++) ((char *)&pwc)[i] = 0;
		pwc.mmio = &m;
		pw1 = parity_power_well_by_id(&pd, PARITY_SKL_DISP_PW_1);

		/* PG0 present now; PG1 distributes only after several fuse reads (late). */
		f.fuse_status = (1u << 27);             /* PG0 dist status */
		f.fuse_delay = 8u; f.fuse_delay_bits = (1u << 26);   /* PG1 appears after 8 reads */
		rc = (pw1 >= 0) ? parity_power_well_enable(&pd.power_wells[pw1], &pwc) : -1;
		KCHECK(rc == 0 && pw1 >= 0 && pd.power_wells[pw1].hw_enabled == 1 &&
			(osdep_mmio_raw_read32(&m, 0x42000u) & (1u << 26)) != 0u,   /* PG1 now observed */
			"fuse: PG1 arrives late -> the fuse wait (fast 2us then slow 1ms) observes it and continues");

		/* PG1 never distributes -> real HW timeout is warned and the enable continues. */
		fake_mmio_open(&m, &f);
		f.fuse_status = (1u << 27);             /* PG0 only; PG1 never appears */
		pd.power_wells[pw1].hw_enabled = -1;
		rc = parity_power_well_enable(&pd.power_wells[pw1], &pwc);
		KCHECK(rc == 0 && pd.power_wells[pw1].hw_enabled == 1,
			"fuse: PG1 never distributes -> HW timeout warned, enable continues (not -EIO)");
	}

	/* --- DMC firmware provider: acquire the fixed reference blob by name --- */
	{
		extern const unsigned parity_fw_adlp_dmc_checksum;
		struct osdep_firmware fw;
		int rc;
		unsigned i;
		uint32_t sum = 0u;

		rc = osdep_request_firmware(&fw, "i915/adlp_dmc.bin");
		if (rc == 0 && fw.data != 0)
			for (i = 0u; i < fw.size; i++) sum += fw.data[i];
		KCHECK(rc == 0 && fw.data != 0 && fw.size == 79088u &&
			sum == parity_fw_adlp_dmc_checksum,
			"dmc-fw: request i915/adlp_dmc.bin returns the fixed 79088-byte reference blob");
		osdep_release_firmware(&fw);
		KCHECK(fw.data == 0 && fw.size == 0u,
			"dmc-fw: release drops the handle (static blob not freed)");

		rc = osdep_request_firmware(&fw, "i915/nonexistent.bin");
		KCHECK(rc != 0 && fw.data == 0,
			"dmc-fw: a genuinely absent firmware returns -errno with no bytes");
	}

	/* --- DMC F1 parser: parse the fixed reference blob into device-owned payloads --- */
	{
		static struct parity_dmc dmc;
		struct osdep_firmware fw;
		int rc, id, mainp;

		rc = osdep_request_firmware(&fw, "i915/adlp_dmc.bin");
		parity_dmc_prepare(&dmc, 13, 'D', '0');   /* ADL-P rev 0x0c -> STEP_D0 */
		rc = (rc == 0) ? parity_parse_dmc_fw(&dmc, fw.data, fw.size) : rc;
		mainp = parity_dmc_has_payload(&dmc);
		kern_logf("i915: DMC-PARSE rc=%d ver=%u.%u pkg_ver=%u entries=%u css_len=%u main=%d\n",
			rc, (unsigned)(dmc.version >> 16), (unsigned)(dmc.version & 0xffff),
			dmc.package_header_ver, dmc.num_entries, dmc.css_header_len_bytes, mainp);
		for (id = 0; id < PARITY_DMC_FW_MAX; id++)
			if (dmc.dmc_info[id].present)
				kern_logf("i915: DMC-PARSE id=%d hv=%d off_dw=%u start=0x%x mmio=%u fwsz_dw=%u payload=%u present=%d\n",
					id, dmc.dmc_info[id].header_ver, dmc.dmc_info[id].dmc_offset,
					dmc.dmc_info[id].start_mmioaddr, dmc.dmc_info[id].mmio_count,
					dmc.dmc_info[id].dmc_fw_size, dmc.dmc_info[id].payload_size,
					dmc.dmc_info[id].present);
		/* payload stays valid after the firmware handle is released. */
		osdep_release_firmware(&fw);
		{
			int allp = dmc.dmc_info[0].present && dmc.dmc_info[1].present &&
				dmc.dmc_info[2].present && dmc.dmc_info[3].present &&
				dmc.dmc_info[4].present;
			KCHECK(rc == 0 && mainp == 1 &&
				dmc.version == ((2u << 16) | 20u) &&     /* v2.20 from CSS */
				dmc.num_entries == 6u && dmc.package_header_ver == 2u &&
				allp &&                                  /* MAIN + 4 pipe DMCs selected */
				dmc.dmc_info[PARITY_DMC_FW_MAIN].header_ver == 3 &&
				dmc.dmc_info[PARITY_DMC_FW_MAIN].payload_size == 25096u &&
				dmc.dmc_info[PARITY_DMC_FW_MAIN].payload != 0,
				"dmc: parse (v2.20) selects MAIN+4 pipes, MAIN payload 25096B saved (valid after fw release)");
		}
		parity_dmc_parse_reset(&dmc);
	}

	/* --- DMC F2 load_program: 13019 payload + 35 aux + 80 evt-disable, per-write verified --- */
	{
		static struct parity_dmc dmc;
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		struct osdep_firmware fw;
		uint32_t dc_state = 0xffffffffu;
		uint64_t epsum = 0u;
		int id, prc;
		unsigned i;

		prc = osdep_request_firmware(&fw, "i915/adlp_dmc.bin");
		parity_dmc_prepare(&dmc, 13, 'D', '0');
		if (prc == 0)
			(void)parity_parse_dmc_fw(&dmc, fw.data, fw.size);

		fake_mmio_open(&m, &f);
		f.wt_total = 0u;
		parity_intel_dmc_load_program(&dmc, &m, &dc_state);

		/*
		 * Expected payload checksum from the RAW blob (fw.data) at the parsed
		 * offset -- INDEPENDENT of the migrated parser's saved copy, so a copy
		 * bug is caught.  readcount = CSS(128) + package(16 + 32*12 = 400) = 528;
		 * a v3 DMC header is 256 bytes.  Order: MAIN..PIPED, i 0..fw_size-1.
		 */
		for (id = 0; id < PARITY_DMC_FW_MAX; id++) {
			struct parity_dmc_info *in = &dmc.dmc_info[id];
			unsigned blob_off;
			if (in->payload == 0)
				continue;
			blob_off = 528u + in->dmc_offset * 4u + 256u;   /* payload start in fw.data */
			for (i = 0u; i < in->dmc_fw_size; i++) {
				const uint8_t *q = fw.data + blob_off + 4u * i;
				uint32_t a = in->start_mmioaddr + i * 4u;
				uint32_t v = (uint32_t)q[0] | ((uint32_t)q[1] << 8) |
					((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
				epsum = epsum * 1000003u + a + (uint64_t)v * 7u;
			}
		}
		KCHECK(dc_state == 0u && dmc.load_seq_completed == 1 &&
			dmc.payload_writes == 13019u && dmc.aux_writes == 35u &&
			dmc.evt_disable_writes == 80u &&
			dmc.psum == epsum &&              /* every payload write vs the RAW blob (addr/val/order/count) */
			f.wt_total == 13141u,
			"dmc: load_program writes 13019 payload + 35 aux + 80 evt-disable (verified vs raw blob)");

		/* Detector: corrupt one saved payload DWORD -> the checksum must diverge. */
		{
			uint64_t bad = 0u;
			uint8_t *mut = (uint8_t *)dmc.dmc_info[PARITY_DMC_FW_MAIN].payload;
			mut[0] ^= 0xffu;   /* flip the MAIN payload's first byte */
			fake_mmio_open(&m, &f);
			parity_intel_dmc_load_program(&dmc, &m, &dc_state);
			bad = dmc.psum;
			mut[0] ^= 0xffu;   /* restore */
			KCHECK(bad != epsum,
				"dmc: a one-DWORD payload change is detected against the fixed reference");
		}
		osdep_release_firmware(&fw);
		parity_dmc_parse_reset(&dmc);
	}

	/* --- DMC F3: async init -> worker -> load, and firmware-absent path --- */
	{
		static struct parity_dmc_dev dd;
		static struct parity_power_domains pd;
		static struct parity_kworkqueue wq;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct parity_pw_ctx pwc;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		uint64_t deadline;
		unsigned i, rc0 = 0u, rc1 = 0u, k;
		int wqok;

		osdep_trace_init(tr);
		fake_mmio_open(&m, &f);
		f.fuse_status = 0xFFFFFFFFu;   /* well enables during the DMC INIT-ref get */
		(void)parity_intel_power_domains_init(&pd, 13u, -1, 1, tr);
		for (i = 0u; i < sizeof(pwc); i++) ((char *)&pwc)[i] = 0;
		pwc.mmio = &m;
		wqok = (parity_kworkqueue_create(&wq, "dmc") == 0);

		/* Sum of all well refcounts, to show the DMC ref nets to zero on success. */
		for (k = 0u; k < pd.num_power_wells; k++) rc0 += pd.power_wells[k].refcount;

		if (wqok) {
			for (i = 0u; i < sizeof(dd); i++) ((char *)&dd)[i] = 0;
			parity_intel_dmc_init(&dd, &wq, &m, &pd, &pwc, 13, 'D', '0', 0 /* default path */);
			deadline = sched_ticks() + 200u;   /* ~2s */
			(void)parity_kflush_work(&wq, &dd.work, deadline);

			for (k = 0u; k < pd.num_power_wells; k++) rc1 += pd.power_wells[k].refcount;
			KCHECK(dd.work_submitted == 1 && dd.worker_started == 1 &&
				dd.firmware_acquired == 1 && dd.main_payload_present == 1 &&
				dd.load_seq_completed_flag == 1 && dd.dmc_wakeref_held == 0 &&
				dd.dmc.payload_writes == 13019u && rc1 == rc0,
				"dmc: DMC-NORMAL worker loads the payload; DMC ref released (nets to 0), no leak");
			parity_intel_dmc_fini(&dd, sched_ticks() + 200u);

			/* DMC-NO-FW: a genuinely absent path -> fallback requested, no payload, ref held. */
			for (i = 0u; i < sizeof(dd); i++) ((char *)&dd)[i] = 0;
			parity_intel_dmc_init(&dd, &wq, &m, &pd, &pwc, 13, 'D', '0', "i915/absent.bin");
			(void)parity_kflush_work(&wq, &dd.work, sched_ticks() + 200u);
			KCHECK(dd.worker_started == 1 && dd.fallback_requested == 1 &&
				dd.main_payload_present == 0 && dd.dmc.payload_writes == 0u &&
				dd.dmc_wakeref_held == 1,          /* held on failure */
				"dmc: DMC-NO-FW requests default+fallback, no load, DMC ref held (blocks rpm)");
			parity_intel_dmc_fini(&dd, sched_ticks() + 200u);
			KCHECK(dd.dmc_wakeref_held == 0,
				"dmc: fini releases the still-held DMC ref (failure path)");
			parity_kworkqueue_destroy(&wq);
		} else {
			KCHECK(0, "dmc: could not create the DMC workqueue");
		}
		parity_intel_power_domains_cleanup(&pd);
	}

	/* --- DMC-BAD-FW / DMC-FINI(running) / DMC-FINI(MMIO fault) --- */
	{
		static struct parity_dmc_dev dd;
		static struct parity_power_domains pd;
		static struct parity_kworkqueue wq;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct parity_pw_ctx pwc;
		struct osdep_trace *tr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		struct osdep_firmware fw;
		unsigned i, k, rc0 = 0u, rc1 = 0u;
		uint64_t b = 0, fr = 0, now = 0, nf = 0;
		int wqok;

		osdep_trace_init(tr);
		fake_mmio_open(&m, &f);
		f.fuse_status = 0xFFFFFFFFu;
		(void)parity_intel_power_domains_init(&pd, 13u, -1, 1, tr);
		for (i = 0u; i < sizeof(pwc); i++) ((char *)&pwc)[i] = 0;
		pwc.mmio = &m;
		wqok = (parity_kworkqueue_create(&wq, "dmc2") == 0);
		for (k = 0u; k < pd.num_power_wells; k++) rc0 += pd.power_wells[k].refcount;

		if (wqok) {
			/* ---- DMC-BAD-FW: corrupt the MAIN DMC header in a test copy ---- */
			if (osdep_request_firmware(&fw, "i915/adlp_dmc.bin") == 0) {
				for (i = 0u; i < 79088u; i++) g_dmc_badcopy[i] = fw.data[i];
				osdep_release_firmware(&fw);
			}
			/* MAIN dmc header at 528 + 6301*4; header_ver is byte 5 -> unknown version 7. */
			g_dmc_badcopy[528u + 6301u * 4u + 5u] = 7u;
			osdep_firmware_test_set(&dmc_bad_ops);
			for (i = 0u; i < sizeof(dd); i++) ((char *)&dd)[i] = 0;
			parity_intel_dmc_init(&dd, &wq, &m, &pd, &pwc, 13, 'D', '0', 0);
			(void)parity_kflush_work(&wq, &dd.work, sched_ticks() + 200u);
			KCHECK(dd.firmware_acquired == 1 && dd.main_payload_present == 0 &&
				dd.dmc.payload_writes == 0u && dd.dmc.load_seq_completed == 0 &&
				dd.dmc_wakeref_held == 1,
				"dmc: DMC-BAD-FW corrupt MAIN header -> no MAIN payload, no program write, ref held");
			parity_intel_dmc_fini(&dd, sched_ticks() + 200u);
			osdep_firmware_test_set(0);   /* override cleared only AFTER the worker is done */
			KCHECK(dd.dmc_wakeref_held == 0 && dd.dmc.dmc_info[PARITY_DMC_FW_PIPEA].payload == 0,
				"dmc: DMC-BAD-FW fini recovers the held ref and the partially used arena");

			/* ---- DMC-FINI (running): park the worker, fini from another thread ---- */
			fake_mmio_open(&m, &f);
			f.fuse_status = 0xFFFFFFFFu;         /* keep fuses present after the reset */
			parity_dmc_test_pause = 1;
			for (i = 0u; i < sizeof(dd); i++) ((char *)&dd)[i] = 0;
			kern_logf("i915: DMC-FINI ckpt A: init\n");
			parity_intel_dmc_init(&dd, &wq, &m, &pd, &pwc, 13, 'D', '0', 0);
			kern_logf("i915: DMC-FINI ckpt B: queued, spawning fini thread\n");
			g_fini_dd = &dd; g_fini_done = 0;
			(void)spawn_detached(dmc_fini_thread, 0);
			kern_logf("i915: DMC-FINI ckpt C: sleeping 40ms\n");
			kern_usleep_range(40000u, 50000u);   /* let the worker reach the park + fini block */
			kern_logf("i915: DMC-FINI ckpt D: ws=%d fini_done=%d\n", dd.worker_started, g_fini_done);
			KCHECK(dd.worker_started == 1 && g_fini_done == 0 &&
				dd.dmc.dmc_info[PARITY_DMC_FW_MAIN].payload != 0,   /* arena NOT freed while running */
				"dmc: DMC-FINI(running) fini blocks in flush; arena/payload intact while the worker runs");
			parity_dmc_test_pause = 0;   /* release the worker */
			kern_logf("i915: DMC-FINI ckpt E: released pause\n");
			(void)kern_rtc_read_counter(&b, &fr);
			while (g_fini_done == 0) {
				if (!kern_rtc_read_counter(&now, &nf) || (fr != 0u && now - b >= fr * 3u)) break;
				kern_usleep_range(1000u, 2000u);
			}
			KCHECK(g_fini_done == 1 && dd.load_seq_completed_flag == 1 &&
				dd.dmc.payload_writes == 13019u && dd.dmc_wakeref_held == 0 &&
				dd.dmc.dmc_info[PARITY_DMC_FW_MAIN].payload == 0,   /* recovered once, after sync */
				"dmc: DMC-FINI(running) after release: worker finishes load, fini syncs then recovers once");

			/* ---- DMC-FINI (MMIO fault mid-write): stop at write 1000, ref held, fini recovers ---- */
			fake_mmio_open(&m, &f);
			f.fuse_status = 0xFFFFFFFFu;
			parity_dmc_test_fault_at = 1000u;
			for (i = 0u; i < sizeof(dd); i++) ((char *)&dd)[i] = 0;
			parity_intel_dmc_init(&dd, &wq, &m, &pd, &pwc, 13, 'D', '0', 0);
			(void)parity_kflush_work(&wq, &dd.work, sched_ticks() + 200u);
			parity_dmc_test_fault_at = 0u;   /* cleared only after the worker is done */
			KCHECK(dd.main_payload_present == 1 && dd.dmc.payload_writes == 1000u &&
				dd.load_seq_completed_flag == 0 && dd.first_fault == 1 &&
				dd.dmc_wakeref_held == 1 &&              /* payload exists but ref still held */
				sched_test_preempt_count() == 0u,        /* preemption restored on abort */
				"dmc: DMC-FINI(fault) aborts at write 1000, restores preempt, keeps the DMC ref");
			parity_intel_dmc_fini(&dd, sched_ticks() + 200u);
			for (k = 0u; k < pd.num_power_wells; k++) rc1 += pd.power_wells[k].refcount;
			KCHECK(dd.dmc_wakeref_held == 0 && rc1 == rc0,
				"dmc: DMC-FINI(fault) fini recovers the held ref by ownership (refcounts net to 0)");
			parity_kworkqueue_destroy(&wq);
		} else {
			KCHECK(0, "dmc: could not create the DMC workqueue (2)");
		}
		parity_intel_power_domains_cleanup(&pd);
	}

	/* --- P3 tail: intel_mode_config_init .. intel_fbc_init (display_state.c) --- */
	{
		static struct parity_display_state ds;
		static struct parity_bw_state dbw;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct mutex dsb;
		unsigned i, k;
		int rc;
		uint16_t m13, m11;

		/*
		 * SAGV fixture.  num_planes is chosen so that tgl_max_bw_index()
		 * (reverse scan, num_planes <= bi->num_planes, fallback 0) and
		 * icl_max_bw_index() (forward scan, num_planes >= bi->num_planes,
		 * fallback UINT_MAX) land on DIFFERENT groups -- they are not
		 * variants of each other, and this fixture proves the port kept
		 * both.  group 0 carries the icl answer, group 5 the tgl answer.
		 */
		for (i = 0u; i < sizeof(dbw); i++) ((char *)&dbw)[i] = 0;
		for (k = 0u; k < (unsigned)PARITY_BW_GROUPS; k++) {
			dbw.max[k].num_qgv_points = 3u;
			dbw.max[k].num_psf_gv_points = 2u;
			dbw.max[k].num_planes = (k == 0u) ? 1u : 4u;
		}
		dbw.max[0].deratedbw[0] = 500u; dbw.max[0].deratedbw[1] = 100u; dbw.max[0].deratedbw[2] = 100u;
		dbw.max[5].deratedbw[0] = 100u; dbw.max[5].deratedbw[1] = 300u; dbw.max[5].deratedbw[2] = 200u;
		dbw.max[0].psf_bw[0] = 50u; dbw.max[0].psf_bw[1] = 50u;   /* tie -> multi-bit mask */
		dbw.valid = 1;

		(void)mutex_init(&dsb, LOCK_RANK_DEVICE, "ktest-ds");

		/* ---- DS-MODE (ADL-P): the modern arms of both ladders ---- */
		for (i = 0u; i < sizeof(ds); i++) ((char *)&ds)[i] = 0;
		parity_intel_mode_config_init(&ds, 13, PARITY_PLAT_NONE);
		KCHECK(ds.mode_config.max_width == 16384u && ds.mode_config.max_height == 16384u &&
			ds.mode_config.cursor_width == 256u && ds.mode_config.cursor_height == 256u &&
			ds.mode_config.min_width == 0u && ds.mode_config.min_height == 0u &&
			ds.mode_config.preferred_depth == 24u && ds.mode_config.prefer_shadow == 1 &&
			ds.mode_config.async_page_flip == 1 &&        /* HAS_ASYNC_FLIPS: ver >= 5 */
			ds.mode_config.funcs_set == 1 && ds.mode_config.helper_private_set == 1 &&
			ds.obj_list_inited == 1 && ds.obj_count == 0u && ds.obj_list_head == 0,
			"ds: DS-MODE mode_config_init(ADL-P) limits + empty global obj_list");

		/* ---- DS-MODE ladders: the branches are real, not constants ---- */
		{
			struct parity_display_state t;
			unsigned ok = 1u;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_mode_config_init(&t, 4, PARITY_PLAT_NONE);
			if (t.mode_config.max_width != 8192u || t.mode_config.async_page_flip != 0) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_mode_config_init(&t, 3, PARITY_PLAT_NONE);
			if (t.mode_config.max_width != 4096u) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_mode_config_init(&t, 2, PARITY_PLAT_NONE);
			if (t.mode_config.max_width != 2048u) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_mode_config_init(&t, 4, PARITY_PLAT_I845G);
			if (t.mode_config.cursor_width != 64u || t.mode_config.cursor_height != 1023u) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_mode_config_init(&t, 4, PARITY_PLAT_I865G);
			if (t.mode_config.cursor_width != 512u || t.mode_config.cursor_height != 1023u) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_mode_config_init(&t, 3, PARITY_PLAT_I915GM);
			if (t.mode_config.cursor_width != 64u || t.mode_config.cursor_height != 64u) ok = 0u;
			KCHECK(ok == 1u,
				"ds: DS-MODE version/cursor ladders take every reference arm");
		}

		/* ---- DS-INDEX: tgl_max_bw_index != icl_max_bw_index ---- */
		m13 = (uint16_t)parity_icl_max_bw_qgv_point_mask(&dbw, 13, 0);
		m11 = (uint16_t)parity_icl_max_bw_qgv_point_mask(&dbw, 11, 0);
		KCHECK(m13 == 0x2u && m11 == 0x1u &&
			parity_icl_max_bw_psf_gv_point_mask(&dbw) == 0x3u &&   /* tie -> both bits */
			parity_icl_qgv_points_mask(&dbw) == 0x307u,
			"ds: DS-INDEX ver>=12 uses tgl index (BIT1), ver<12 uses icl index (BIT0); psf tie ORs");

		/* ---- DS-OBJ: the four global objects, in reference order ---- */
		for (i = 0u; i < sizeof(ds); i++) ((char *)&ds)[i] = 0;
		parity_intel_mode_config_init(&ds, 13, PARITY_PLAT_NONE);
		rc = parity_intel_cdclk_init(&ds);
		rc |= parity_intel_color_init(&ds, 13);
		rc |= parity_intel_dbuf_init(&ds);
		fake_mmio_open(&m, &f);
		{
			static const struct fake_pcode_txn sagv_ok[] = {
				{ 0xeu, 0x5u, 0x0u, 0u, 0x0u, 0u },   /* SAGV cfg, points_mask 0x5, safe reply */
			};
			f.ptxn = sagv_ok; f.ptxn_len = 1u; f.ptxn_i = 0u;
			dbw.sagv_status = (int)PARITY_SAGV_ENABLED;
			rc |= parity_intel_bw_init(&ds, 13, &dbw, &dsb, &m);
		}
		rc |= parity_intel_pmdemand_init(&ds, 13, &m, -1);
		KCHECK(rc == 0 && ds.obj_count == 4u && ds.color_done == 1 &&
			ds.obj_list_head == &ds.cdclk_obj &&
			ds.cdclk_obj.next == &ds.dbuf_obj &&
			ds.dbuf_obj.next == &ds.bw_obj &&
			ds.bw_obj.next == &ds.pmdemand_obj &&
			ds.pmdemand_obj.next == 0 && ds.obj_list_tail == &ds.pmdemand_obj,
			"ds: DS-OBJ global obj_list order is cdclk,dbuf,bw,pmdemand (tail-appended)");
		KCHECK(ds.cdclk_obj.state == &ds.cdclk_state.base &&
			ds.cdclk_state.base.obj == &ds.cdclk_obj &&
			ds.cdclk_state.base.ref == 1u && ds.bw_obj_state.base.ref == 1u &&
			ds.pmdemand_state.base.obj == &ds.pmdemand_obj &&
			ds.cdclk_obj.funcs != ds.dbuf_obj.funcs &&
			ds.bw_obj.funcs != ds.pmdemand_obj.funcs &&
			ds.pmdemand_wa_14016740474 == 0,   /* ver-14 WA never on ADL-P */
			"ds: DS-OBJ each state back-links its obj, kref=1, funcs distinct, no ver14 WA");

		/* ---- DS-SAGV: the forced disable is a real PCODE transaction ---- */
		KCHECK(f.ptxn_i == 1u && f.ptxn_bad == 0 &&
			ds.sagv_force_disable_attempted == 1 &&
			ds.sagv_qgv_points == 0x2u && ds.sagv_psf_points == 0x3u &&
			ds.bw_obj_state.qgv_points_mask == 0x5u &&   /* ~(0x2|0x300) & 0x307 */
			ds.sagv_pcode_ret == 0 &&
			dbw.sagv_status == (int)PARITY_SAGV_DISABLED,  /* ~0x5 & 0x307 & 0xff = 0x2, pow2 */
			"ds: DS-SAGV bw_init forces SAGV off via PCODE 0xe data 0x5, status -> DISABLED");

		/* ---- DS-SAGV-SKIP: NOT_CONTROLLED -> no PCODE at all ---- */
		for (i = 0u; i < sizeof(ds); i++) ((char *)&ds)[i] = 0;
		parity_intel_mode_config_init(&ds, 13, PARITY_PLAT_NONE);
		fake_mmio_open(&m, &f);
		f.ptxn = 0; f.ptxn_len = 0u; f.ptxn_i = 0u;   /* any PCODE txn fails the test */
		dbw.sagv_status = (int)PARITY_SAGV_NOT_CONTROLLED;
		rc = parity_intel_bw_init(&ds, 13, &dbw, &dsb, &m);
		KCHECK(rc == 0 && ds.obj_count == 1u && f.pcode_txn_count == 0u &&
			ds.sagv_force_disable_attempted == 0 &&
			dbw.sagv_status == (int)PARITY_SAGV_NOT_CONTROLLED &&
			parity_intel_has_sagv(13, PARITY_PLAT_NONE, (int)PARITY_SAGV_NOT_CONTROLLED) == 0 &&
			parity_intel_has_sagv(13, PARITY_PLAT_NONE, (int)PARITY_SAGV_ENABLED) == 1 &&
			parity_intel_has_sagv(8, PARITY_PLAT_NONE, (int)PARITY_SAGV_ENABLED) == 0,
			"ds: DS-SAGV-SKIP NOT_CONTROLLED (or ver<9) skips the forced disable entirely");

		/* ---- DS-SAGV-FAIL: PCODE error is logged, bw_init still returns 0 ---- */
		for (i = 0u; i < sizeof(ds); i++) ((char *)&ds)[i] = 0;
		parity_intel_mode_config_init(&ds, 13, PARITY_PLAT_NONE);
		fake_mmio_open(&m, &f);
		f.pcode_sticky_status = 0x2;   /* every txn returns a PCODE error */
		dbw.sagv_status = (int)PARITY_SAGV_ENABLED;
		rc = parity_intel_bw_init(&ds, 13, &dbw, &dsb, &m);
		KCHECK(rc == 0 &&                                  /* icl_force_disable_sagv is void */
			ds.sagv_force_disable_attempted == 1 && ds.sagv_pcode_ret != 0 &&
			dbw.sagv_status == (int)PARITY_SAGV_ENABLED && /* NOT updated on failure */
			ds.obj_count == 1u,
			"ds: DS-SAGV-FAIL PCODE error leaves sagv.status untouched; bw_init still succeeds");

		/* ---- DS-ENOMEM: the reference's -ENOMEM propagation stays reachable ---- */
		for (i = 0u; i < sizeof(ds); i++) ((char *)&ds)[i] = 0;
		parity_intel_mode_config_init(&ds, 13, PARITY_PLAT_NONE);
		fake_mmio_open(&m, &f);
		parity_display_state_test_alloc_fail_at = 3u;   /* the bw state */
		rc = parity_intel_cdclk_init(&ds);
		rc |= parity_intel_dbuf_init(&ds);
		dbw.sagv_status = (int)PARITY_SAGV_ENABLED;
		k = (unsigned)parity_intel_bw_init(&ds, 13, &dbw, &dsb, &m);
		parity_display_state_test_alloc_fail_at = 0u;
		KCHECK(rc == 0 && (int)k == -ENOMEM && ds.obj_count == 2u &&
			ds.obj_list_tail == &ds.dbuf_obj &&
			ds.sagv_force_disable_attempted == 0 &&   /* failed before the PCODE */
			ds.fail_where != 0,
			"ds: DS-ENOMEM a failed state alloc aborts that init, obj_list unchanged");

		/* ---- DS-COLOR: ver 10 is unimplemented, not a silent success ---- */
		{
			struct parity_display_state t;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			KCHECK(parity_intel_color_init(&t, 13) == 0 && t.color_done == 1 &&
				parity_intel_color_init(&t, 10) == -ENOSYS,
				"ds: DS-COLOR ver!=10 returns 0; ver==10 reports -ENOSYS (no silent success)");
		}

		/* ---- DS-QUIRKS ---- */
		{
			struct parity_display_state t;
			unsigned ok = 1u;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_init_quirks(&t, 0x46a8u, 0x8086u, 0x2212u);   /* ADL-P */
			if (t.quirk_mask != 0u || t.quirk_hooks_fired != 0u ||
			    t.dmi_scanned != 1 || t.dmi_available != 0) ok = 0u;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_init_quirks(&t, 0x0a06u, 0x1025u, 0x0a11u);   /* Acer C720 */
			if (t.quirk_mask != (1u << PARITY_QUIRK_BACKLIGHT_PRESENT) ||
			    t.quirk_hooks_fired != 1u) ok = 0u;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_init_quirks(&t, 0x0a06u, 0x1025u, 0x9999u);   /* right dev, wrong subdev */
			if (t.quirk_mask != 0u) ok = 0u;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_display_state_test_dmi_match = 2;                   /* Lillipup entry */
			parity_intel_init_quirks(&t, 0x46a8u, 0x8086u, 0x2212u);
			parity_display_state_test_dmi_match = 0;
			if (t.quirk_mask != (1u << PARITY_QUIRK_NO_PPS_BACKLIGHT_POWER_HOOK) ||
			    t.dmi_available != 1) ok = 0u;

			KCHECK(ok == 1u,
				"ds: DS-QUIRKS ADL-P matches nothing; PCI + DMI entries fire on an exact match");
		}

		/* ---- DS-FBC ---- */
		{
			struct parity_display_state t;
			unsigned ok = 1u;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			t.enable_fbc_param = -1;
			parity_intel_fbc_init(&t, 13, 0x1u, 0, PARITY_PLAT_NONE);   /* ADL-P */
			if (t.fbc_created != 1u || t.fbc[0] == 0 || t.fbc[1] != 0 ||
			    t.fbc[0]->id != PARITY_FBC_A ||
			    t.fbc[0]->funcs_kind != PARITY_FBC_FUNCS_IVB ||
			    t.fbc[0]->lock_inited != 1 ||
			    t.enable_fbc_sanitized != 1 || t.fbc_vtd_wa != 0) ok = 0u;

			/* enable_fbc=0 sanitizes to 0 but the instances are still created. */
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			t.enable_fbc_param = 0;
			parity_intel_fbc_init(&t, 13, 0x3u, 0, PARITY_PLAT_NONE);
			if (t.enable_fbc_sanitized != 0 || t.fbc_created != 2u ||
			    t.fbc[1] == 0 || t.fbc[1]->id != PARITY_FBC_B) ok = 0u;

			/* No FBC in the runtime mask -> HAS_FBC false -> sanitized 0, none created. */
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			t.enable_fbc_param = -1;
			parity_intel_fbc_init(&t, 13, 0x0u, 0, PARITY_PLAT_NONE);
			if (t.enable_fbc_sanitized != 0 || t.fbc_created != 0u) ok = 0u;

			/* WaFbcTurnOffFbcWhenHyperVisorIsUsed: skl/bxt + VT-d only. */
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			t.enable_fbc_param = -1;
			parity_intel_fbc_init(&t, 9, 0x1u, 1, PARITY_PLAT_SKYLAKE);
			if (t.fbc_vtd_wa != 1 || t.fbc_mask != 0u || t.fbc_created != 0u) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			t.enable_fbc_param = -1;
			parity_intel_fbc_init(&t, 13, 0x1u, 1, PARITY_PLAT_NONE);   /* VT-d but not skl/bxt */
			if (t.fbc_vtd_wa != 0 || t.fbc_created != 1u) ok = 0u;

			/* funcs ladder */
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_fbc_init(&t, 6, 0x1u, 0, PARITY_PLAT_NONE);
			if (t.fbc[0] == 0 || t.fbc[0]->funcs_kind != PARITY_FBC_FUNCS_SNB) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_fbc_init(&t, 5, 0x1u, 0, PARITY_PLAT_NONE);
			if (t.fbc[0] == 0 || t.fbc[0]->funcs_kind != PARITY_FBC_FUNCS_ILK) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_fbc_init(&t, 4, 0x1u, 0, PARITY_PLAT_NONE);
			if (t.fbc[0] == 0 || t.fbc[0]->funcs_kind != PARITY_FBC_FUNCS_I965) ok = 0u;
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_fbc_init(&t, 3, 0x1u, 0, PARITY_PLAT_NONE);
			if (t.fbc[0] == 0 || t.fbc[0]->funcs_kind != PARITY_FBC_FUNCS_I8XX) ok = 0u;

			/* enable_fbc is a tri-state: BDW is the only pre-gen9 yes. */
			if (parity_intel_sanitize_fbc_option(8, PARITY_PLAT_BROADWELL, 0x1u, -1) != 1 ||
			    parity_intel_sanitize_fbc_option(8, PARITY_PLAT_NONE, 0x1u, -1) != 0 ||
			    parity_intel_sanitize_fbc_option(13, PARITY_PLAT_NONE, 0x0u, 1) != 1) ok = 0u;

			KCHECK(ok == 1u,
				"ds: DS-FBC vtd WA / sanitize tri-state / per-id create / funcs ladder");
		}

		/* ---- DS-FINI ---- */
		for (i = 0u; i < sizeof(ds); i++) ((char *)&ds)[i] = 0;
		parity_intel_mode_config_init(&ds, 13, PARITY_PLAT_NONE);
		(void)parity_intel_cdclk_init(&ds);
		(void)parity_intel_dbuf_init(&ds);
		ds.enable_fbc_param = -1;
		parity_intel_fbc_init(&ds, 13, 0x1u, 0, PARITY_PLAT_NONE);
		parity_intel_display_state_fini(&ds);
		KCHECK(ds.obj_count == 0u && ds.obj_list_head == 0 && ds.obj_list_tail == 0 &&
			ds.cdclk_obj.next == 0 && ds.cdclk_obj.state == 0 &&
			ds.cdclk_state.base.ref == 0u && ds.dbuf_state.base.ref == 0u &&
			ds.fbc[0] == 0 && ds.fbc_created == 0u && ds.inited == 0,
			"ds: DS-FINI global objs unlinked + refs dropped + fbc released");
	}

	/* --- P4: intel_detect_pch + gen11 IRQ reset/postinstall (pch.c / irq.c) --- */
	{
		struct parity_pch_state pc;
		struct parity_irq_dev id;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		static struct parity_power_domains ipd;
		struct parity_pw_ctx ipwc;
		struct osdep_trace *itr = &ktest_trace_pool[0];   /* 32 KiB ring: shared static, never on the stack */
		unsigned i, k;
		int mi, si, ei;

		/* ---- IRQ-MASKS: the ADL-P mask helpers resolve to the reference values ---- */
		KCHECK(parity_gen8_de_pipe_fault_mask(13) == 0x00100F80u &&   /* RKL_ fault set */
			parity_gen8_de_pipe_fault_mask(11) == 0x00700F80u &&  /* GEN11_ fault set */
			parity_gen8_de_pipe_fault_mask(9)  == 0x00000F80u &&
			parity_gen8_de_pipe_fault_mask(8)  == 0x00000700u &&
			parity_gen8_de_port_aux_mask(13)   == 0x00003F07u &&
			parity_gen8_de_pipe_underrun_mask(13) == 0x80600000u &&
			parity_gen8_de_pipe_underrun_mask(12) == 0x80000000u &&
			parity_gen8_de_pipe_flip_done_mask(13) == 0x00000008u,
			"p4: IRQ-MASKS gen8_de_* helpers match the reference for ADL-P (and neighbours)");

		/* ---- PCH-QEMU: the q35 ISA bridge is virtual -> platform guess = ADP ---- */
		{
			static const struct ktest_bridge qemu_q35[] = {
				{ 0x8086u, 0x2918u, 0x1af4u, 0x1100u },   /* ICH9 LPC, Red Hat/QEMU */
			};
			g_brs = qemu_q35; g_brs_n = 1u;
			parity_pch_test_set_bridges(&ktest_bridge_ops);
			for (i = 0u; i < sizeof(pc); i++) ((char *)&pc)[i] = 0;
			parity_intel_detect_pch(&pc, 13, 1, 1, 1);
			KCHECK(pc.type == PARITY_PCH_ADP && pc.id == 0x7A80u &&
				pc.source == PARITY_PCH_SRC_VIRT && pc.bridges_scanned == 1u &&
				pc.type >= PARITY_PCH_ICP,   /* the >= ICP gate P4 depends on */
				"p4: PCH-QEMU q35 ISA bridge 0x2918 is virtual -> PCH_ADP (>= PCH_ICP)");
		}

		/* ---- PCH-REAL / PCH-SKIP / PCH-NONE ---- */
		{
			static const struct ktest_bridge real_adp[] = {
				{ 0x1234u, 0x0001u, 0u, 0u },             /* non-Intel: skipped */
				{ 0x8086u, 0x7A83u, 0u, 0u },             /* masks to 0x7A80 = ADP */
			};
			static const struct ktest_bridge qemu_wrong_subsys[] = {
				{ 0x8086u, 0x2918u, 0x8086u, 0x1234u },   /* not Red Hat/QEMU */
			};
			unsigned ok = 1u;

			g_brs = real_adp; g_brs_n = 2u;
			for (i = 0u; i < sizeof(pc); i++) ((char *)&pc)[i] = 0;
			parity_intel_detect_pch(&pc, 13, 1, 1, 1);
			if (pc.type != PARITY_PCH_ADP || pc.id != 0x7A80u ||
			    pc.source != PARITY_PCH_SRC_REAL || pc.bridges_scanned != 2u) ok = 0u;

			/* An Intel ISA bridge that is neither a known PCH nor virtual: no match,
			 * the walk ends, and the no-bridge guest guess does NOT run (a bridge
			 * was seen).  PCH stays NONE. */
			g_brs = qemu_wrong_subsys; g_brs_n = 1u;
			for (i = 0u; i < sizeof(pc); i++) ((char *)&pc)[i] = 0;
			parity_intel_detect_pch(&pc, 13, 1, 1, 1);
			if (pc.type != PARITY_PCH_NONE || pc.bridges_scanned != 1u) ok = 0u;

			/* No ISA bridge at all + guest -> platform guess. */
			g_brs = real_adp; g_brs_n = 0u;
			for (i = 0u; i < sizeof(pc); i++) ((char *)&pc)[i] = 0;
			parity_intel_detect_pch(&pc, 13, 1, 1, 1);
			if (pc.type != PARITY_PCH_ADP || pc.source != PARITY_PCH_SRC_NO_BRIDGE) ok = 0u;

			/* Bridge present but no display -> PCH_NOP. */
			g_brs = real_adp; g_brs_n = 2u;
			for (i = 0u; i < sizeof(pc); i++) ((char *)&pc)[i] = 0;
			parity_intel_detect_pch(&pc, 13, 1, 0 /* no display */, 1);
			if (pc.type != PARITY_PCH_NOP || pc.id != 0u) ok = 0u;

			/* The id table itself. */
			if (parity_intel_pch_type(0x3480u) != PARITY_PCH_ICP ||
			    parity_intel_pch_type(0x4D80u) != PARITY_PCH_ICP ||   /* JSP is ICP */
			    parity_intel_pch_type(0xA080u) != PARITY_PCH_TGP ||
			    parity_intel_pch_type(0x4B00u) != PARITY_PCH_TGP ||   /* MCC is TGP */
			    parity_intel_pch_type(0x1e00u) != PARITY_PCH_CPT ||   /* PPT is CPT */
			    parity_intel_pch_type(0xA380u) != PARITY_PCH_SPT ||   /* CMP-V is SPT */
			    parity_intel_pch_type(0x0000u) != PARITY_PCH_NONE) ok = 0u;
			if (parity_intel_is_virt_pch(0x7100u, 0u, 0u) != 1 ||
			    parity_intel_is_virt_pch(0x2900u, 0x1af4u, 0x1100u) != 1 ||
			    parity_intel_is_virt_pch(0x2900u, 0x8086u, 0x1100u) != 0) ok = 0u;

			parity_pch_test_set_bridges(0);
			KCHECK(ok == 1u,
				"p4: PCH real/virt/absent/NOP paths + the id table and virt predicate");
		}

		/* ---- IRQ-RESET: gen11_irq_reset masks and clears every block ---- */
		osdep_trace_init(itr);
		fake_mmio_open(&m, &f);
		f.fuse_status = 0xFFFFFFFFu;
		(void)parity_intel_power_domains_init(&ipd, 13u, -1, 1, itr);
		for (i = 0u; i < sizeof(ipwc); i++) ((char *)&ipwc)[i] = 0;
		ipwc.mmio = &m;
		/* Mark every well as HW-enabled so the pipe/transcoder gates let the
		 * reset and postinstall through (they use the CACHED state). */
		for (k = 0u; k < ipd.num_power_wells; k++) ipd.power_wells[k].hw_enabled = 1;

		for (i = 0u; i < sizeof(pc); i++) ((char *)&pc)[i] = 0;
		pc.type = PARITY_PCH_ADP;
		for (i = 0u; i < sizeof(id); i++) ((char *)&id)[i] = 0;
		id.m = &m; id.pd = &ipd; id.pwc = &ipwc; id.pch = &pc;
		id.display_ver = 13; id.pipe_mask = 0xfu; id.cpu_transcoder_mask = 0xfu;
		id.has_display = 1; id.submission = PARITY_SUBMISSION_EXECLISTS;

		f.wt_n = 0u;
		parity_intel_irq_reset(&id);
		KCHECK(fake_wt_find(&f, 0x190010u, 0u, 0xffffffffu) == 0 &&        /* master first */
			fake_wt_find(&f, 0x190030u, 0u, 0xffffffffu) > 0 &&        /* RENDER_COPY ena=0 */
			fake_wt_find(&f, 0x190090u, 0xffffffffu, 0xffffffffu) > 0 &&  /* RCS0 mask=~0 */
			fake_wt_find(&f, 0x1900d0u, 0xffffffffu, 0xffffffffu) > 0 &&  /* VECS0_VECS1 */
			fake_wt_find(&f, 0x44200u, 0u, 0xffffffffu) > 0 &&         /* DISPLAY_INT_CTL=0 */
			fake_wt_find(&f, 0x60814u, 0xffffffffu, 0xffffffffu) > 0 && /* TRANS_PSR_IMR(A) */
			fake_wt_find(&f, 0x63818u, 0xffffffffu, 0xffffffffu) > 0 && /* TRANS_PSR_IIR(D) */
			fake_wt_find(&f, 0x44404u, 0xffffffffu, 0xffffffffu) > 0 && /* DE_PIPE_IMR(A) */
			fake_wt_find(&f, 0x44434u, 0xffffffffu, 0xffffffffu) > 0 && /* DE_PIPE_IMR(D) */
			fake_wt_find(&f, 0xc4004u, 0xffffffffu, 0xffffffffu) > 0 && /* SDEIMR (>= ICP) */
			fake_wt_find(&f, 0x444f4u, 0xffffffffu, 0xffffffffu) > 0 && /* GU_MISC_IMR */
			fake_wt_find(&f, 0x444e4u, 0xffffffffu, 0xffffffffu) > 0,   /* PCU_IMR */
			"p4: IRQ-RESET master disabled first, then GT/display/GU_MISC/PCU all masked");

		/* gen3_irq_reset writes IIR TWICE (the reference is deliberately paranoid). */
		{
			unsigned iir_writes = 0u;
			for (i = 0u; i < f.wt_n; i++)
				if (f.wt_off[i] == 0x44408u && f.wt_val[i] == 0xffffffffu)
					iir_writes++;
			KCHECK(iir_writes == 2u,
				"p4: IRQ-RESET gen3_irq_reset clears each IIR twice");
		}

		/* ---- IRQ-POST: gen11_irq_postinstall enables the right sources ---- */
		f.wt_n = 0u;
		parity_intel_irq_postinstall(&id);
		mi = fake_wt_find(&f, 0x190030u, 0x09090909u, 0xffffffffu);  /* RENDER_COPY dmask */
		si = fake_wt_find(&f, 0x190090u, ~0x09090000u, 0xffffffffu); /* RCS0 ~smask */
		ei = fake_wt_find(&f, 0x190010u, 0x80000000u, 0xffffffffu);  /* master enable LAST */
		KCHECK(id.gt_irqs == 0x909u && id.gt_dmask == 0x09090909u &&
			id.gt_smask == 0x09090000u &&
			mi >= 0 && si >= 0 && ei >= 0 && ei > mi && ei > si &&
			id.reached_master_enable == 1,
			"p4: IRQ-POST execlists irqs=0x909, dmask/smask written, master enabled LAST");
		KCHECK(id.de_pipe_masked == 0x10100F80u &&
			id.de_pipe_enables == 0x90700F89u &&
			id.de_port_masked == 0x00003F07u &&
			id.de_misc_masked == 0x00080000u &&      /* EDP_PSR only (ver >= 11) */
			id.de_irq_mask[0] == ~0x10100F80u &&
			fake_wt_find(&f, 0x4440cu, 0x90700F89u, 0xffffffffu) >= 0 &&  /* DE_PIPE_IER(A) */
			fake_wt_find(&f, 0xc400cu, 0xffffffffu, 0xffffffffu) >= 0 &&  /* SDEIER (icp) */
			fake_wt_find(&f, 0x4447cu, 0x003F003Fu, 0xffffffffu) >= 0 &&  /* DE_HPD_IER */
			fake_wt_find(&f, 0x44200u, 0x80000000u, 0xffffffffu) >= 0,    /* DISPLAY_INT_CTL */
			"p4: IRQ-POST ADL-P DE masks + per-pipe IER + icp SDE + TC/TBT hotplug");

		/* ---- IRQ-GUC: the GuC-submission arm drops the three CS interrupts ---- */
		{
			struct parity_irq_dev g;

			for (i = 0u; i < sizeof(g); i++) ((char *)&g)[i] = 0;
			g.m = &m; g.pd = &ipd; g.pwc = &ipwc; g.pch = &pc;
			g.display_ver = 13; g.pipe_mask = 0xfu; g.cpu_transcoder_mask = 0xfu;
			g.has_display = 0;   /* GT only for this check */
			g.submission = PARITY_SUBMISSION_GUC;
			parity_gen11_gt_irq_postinstall(&g);
			KCHECK(g.gt_irqs == 0x1u && g.gt_dmask == 0x00010001u,
				"p4: IRQ-GUC guc-submission leaves out CS_MASTER_ERROR/CTX_SWITCH/SEMAPHORE");
		}

		/* ---- IRQ-POWEROFF: pipes/transcoders whose power is off are skipped ---- */
		{
			for (k = 0u; k < ipd.num_power_wells; k++)
				ipd.power_wells[k].hw_enabled = ipd.power_wells[k].always_on ? 1 : 0;
			f.wt_n = 0u;
			parity_gen11_display_irq_reset(&id);
			/*
			 * PIPE_A belongs to PW_A, which is off -> pipe A is skipped.
			 * TRANSCODER_A is claimed by NO dedicated well; only the
			 * always-on well covers it, and the reference SKIPS always-on
			 * wells when deciding, so the domain still counts as enabled and
			 * the PSR registers are reset.  Blocks with no power gate at all
			 * (DE_PORT) are always reset.
			 */
			KCHECK(fake_wt_find(&f, 0x44200u, 0u, 0xffffffffu) >= 0 &&      /* still gated off */
				fake_wt_find(&f, 0x44404u, 0xffffffffu, 0xffffffffu) < 0 && /* pipe A skipped */
				fake_wt_find(&f, 0x44434u, 0xffffffffu, 0xffffffffu) < 0 && /* pipe D skipped */
				fake_wt_find(&f, 0x60814u, 0xffffffffu, 0xffffffffu) >= 0 && /* trans A: always-on */
				fake_wt_find(&f, 0x61814u, 0xffffffffu, 0xffffffffu) < 0 && /* trans B: PW_B off */
				fake_wt_find(&f, 0x44444u, 0xffffffffu, 0xffffffffu) >= 0,  /* DE_PORT ungated */
				"p4: IRQ-POWEROFF unpowered pipes skipped; always-on-only domains still processed");
			for (k = 0u; k < ipd.num_power_wells; k++) ipd.power_wells[k].hw_enabled = 1;
		}

		/* ---- IRQ-ACK: every asserted display source is read AND acked ---- */
		{
			uint32_t di_ctl;

			/*
			 * Assert MISC + HPD + PORT + PIPE_A/B + PCH in DISPLAY_INT_CTL and
			 * give each block a non-zero IIR.  Pipe A carries VBLANK|flip-done|
			 * underrun|a fault bit so each decode arm is exercised at once.
			 */
			di_ctl = (1u << 22) | (1u << 21) | (1u << 20) |
				 (1u << 16) | (1u << 17) | (1u << 23);
			osdep_mmio_raw_write32(&m, 0x44200u, di_ctl);
			osdep_mmio_raw_write32(&m, 0x44468u, 0x00080000u);   /* DE_MISC_IIR  */
			osdep_mmio_raw_write32(&m, 0x44478u, 0x00000001u);   /* DE_HPD_IIR   */
			osdep_mmio_raw_write32(&m, 0x44448u, 0x00000001u);   /* DE_PORT_IIR  */
			osdep_mmio_raw_write32(&m, 0x44408u, 0x80000009u);   /* PIPE_A: underrun|flip|vblank */
			osdep_mmio_raw_write32(&m, 0x44418u, 0x00000080u);   /* PIPE_B: a fault bit */
			osdep_mmio_raw_write32(&m, 0xc4008u, 0x00800000u);   /* SDEIIR: GMBUS */
			f.wt_n = 0u;
			parity_gen11_display_irq_handler(&id);

			KCHECK(id.de_misc_acks == 1u && id.de_hpd_acks == 1u &&
				id.de_port_acks == 1u && id.de_pch_acks == 1u &&
				id.de_pipe_iir_acks[0] == 1u && id.de_pipe_iir_acks[1] == 1u &&
				id.de_vblank_count[0] == 1u && id.de_flip_done_count == 1u &&
				id.de_underrun_count == 1u && id.de_fault_count == 1u &&
				id.de_lied_count == 0u && id.last_disp_ctl == di_ctl,
				"p4: IRQ-ACK each asserted display source is read, acked and decoded");
			KCHECK(fake_wt_find(&f, 0x44468u, 0x00080000u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x44478u, 0x00000001u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x44448u, 0x00000001u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x44408u, 0x80000009u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x44418u, 0x00000080u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0xc4008u, 0x00800000u, 0xffffffffu) >= 0,
				"p4: IRQ-ACK the ack is a write-back of the IIR value just read");
			/* The display block is gated off across the read/ack and re-enabled. */
			KCHECK(fake_wt_find(&f, 0x44200u, 0u, 0xffffffffu) == 0 &&
				fake_wt_find(&f, 0x44200u, 0x80000000u, 0xffffffffu) > 0,
				"p4: IRQ-ACK DISPLAY_INT_CTL is gated off first and re-enabled last");

			/* A master bit set with a zero IIR is the reference's "lied" case. */
			id.de_lied_count = 0u; id.de_misc_acks = 0u;
			osdep_mmio_raw_write32(&m, 0x44200u, (1u << 22));
			osdep_mmio_raw_write32(&m, 0x44468u, 0u);
			parity_gen11_display_irq_handler(&id);
			KCHECK(id.de_lied_count == 1u && id.de_misc_acks == 0u,
				"p4: IRQ-ACK a master bit with a zero IIR is counted as lied, not acked");

			/* A source NOT asserted in the master word is never touched. */
			osdep_mmio_raw_write32(&m, 0x44200u, 0u);
			osdep_mmio_raw_write32(&m, 0x44448u, 0x00000001u);
			f.wt_n = 0u;   /* trace only the handler, not this setup */
			parity_gen11_display_irq_handler(&id);
			KCHECK(fake_wt_find(&f, 0x44448u, 0x00000001u, 0xffffffffu) < 0,
				"p4: IRQ-ACK an unasserted source is not read or acked");
		}

		/* ---- IRQ-NODISPLAY: HAS_DISPLAY=0 touches no display register ---- */
		{
			struct parity_irq_dev n;

			for (i = 0u; i < sizeof(n); i++) ((char *)&n)[i] = 0;
			n.m = &m; n.pd = &ipd; n.pwc = &ipwc; n.pch = &pc;
			n.display_ver = 13; n.pipe_mask = 0xfu; n.cpu_transcoder_mask = 0xfu;
			n.has_display = 0;
			f.wt_n = 0u;
			parity_gen11_display_irq_reset(&n);
			parity_gen11_de_irq_postinstall(&n);
			KCHECK(f.wt_n == 0u,
				"p4: IRQ-NODISPLAY display reset/postinstall are no-ops without a display");
		}

		parity_intel_power_domains_cleanup(&ipd);
	}

	/* --- P5-a: the front of intel_display_driver_probe_nogem (display_nogem.c) --- */
	{
		static struct parity_display_nogem ng;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct mutex nsb;
		unsigned i;

		(void)mutex_init(&nsb, LOCK_RANK_DEVICE, "ktest-nogem");

		/* ---- P5A-WM: the two PCODE latency reads and the adjust rules ---- */
		{
			/*
			 * data0=0 -> levels 0..3, data0=1 -> levels 4..7.  Level 0 is
			 * non-zero here, so WaWmMemoryReadLatency must NOT add the read
			 * latency; level 6 is 0, so levels 6..7 must be zeroed.
			 */
			static const struct fake_pcode_txn lat[] = {
				{ 0x6u, 0x0u, 0x0e0a0602u, 0u, 0x0u, 0u },   /* 2,6,10,14 */
				{ 0x6u, 0x1u, 0x00001a16u, 0u, 0x0u, 0u },   /* 22,26,0,0 */
			};
			fake_mmio_open(&m, &f);
			f.ptxn = lat; f.ptxn_len = 2u; f.ptxn_i = 0u;
			for (i = 0u; i < sizeof(ng); i++) ((char *)&ng)[i] = 0;
			parity_skl_setup_wm_latency(&ng, 13, &nsb, &m, 0);
			KCHECK(f.ptxn_i == 2u && f.ptxn_bad == 0 &&
				ng.wm_num_levels == 6u &&      /* HAS_HW_SAGV_WM -> 6, not 8 */
				ng.wm_skl_latency[0] == 2u && ng.wm_skl_latency[1] == 6u &&
				ng.wm_skl_latency[2] == 10u && ng.wm_skl_latency[3] == 14u &&
				ng.wm_skl_latency[4] == 22u && ng.wm_skl_latency[5] == 26u &&
				ng.wm_latency_valid == 1,
				"p5a: P5A-WM two PCODE reads (data0=0/1) decode 8 latencies, 6 levels");
		}
		{
			uint16_t wm[8];
			unsigned ok = 1u;

			/* level 0 == 0 -> add read_latency to every valid level. */
			wm[0] = 0u; wm[1] = 4u; wm[2] = 8u; wm[3] = 12u;
			wm[4] = 16u; wm[5] = 20u; wm[6] = 24u; wm[7] = 28u;
			parity_adjust_wm_latency(wm, 6, 3, 0);
			if (wm[0] != 3u || wm[1] != 7u || wm[5] != 23u) ok = 0u;

			/* a zero at level n>=1 disables n.. and stops the read-latency add. */
			wm[0] = 2u; wm[1] = 4u; wm[2] = 0u; wm[3] = 12u;
			wm[4] = 16u; wm[5] = 20u; wm[6] = 24u; wm[7] = 28u;
			parity_adjust_wm_latency(wm, 6, 3, 0);
			if (wm[2] != 0u || wm[3] != 0u || wm[4] != 0u || wm[5] != 0u ||
			    wm[0] != 2u || wm[1] != 4u) ok = 0u;

			/* the 16GB-DIMM WA adds 1 to level 0 only. */
			wm[0] = 5u; wm[1] = 9u; wm[2] = 13u; wm[3] = 17u;
			wm[4] = 21u; wm[5] = 25u; wm[6] = 0u; wm[7] = 0u;
			parity_adjust_wm_latency(wm, 6, 3, 1);
			if (wm[0] != 6u || wm[1] != 9u) ok = 0u;

			KCHECK(ok == 1u,
				"p5a: P5A-WM adjust_wm_latency zero-truncation, read-latency add, DIMM WA");
		}

		/* ---- P5A-DPLL: adlp_plls is 7 entries with the reference ids ---- */
		for (i = 0u; i < sizeof(ng); i++) ((char *)&ng)[i] = 0;
		parity_intel_shared_dpll_init(&ng, 13, 1);
		KCHECK(ng.dpll_mgr_present == 1 && ng.num_dplls == 7u &&
			ng.dplls[0].id == 0 && ng.dplls[0].enable_reg == 0x46010u &&
			ng.dplls[1].id == 1 && ng.dplls[1].enable_reg == 0x46014u &&
			ng.dplls[2].id == 2 && ng.dplls[2].enable_reg == 0x46020u &&
			ng.dplls[2].funcs == PARITY_DPLL_FUNCS_TBT &&
			ng.dplls[3].enable_reg == 0x46030u &&
			ng.dplls[6].id == 6 && ng.dplls[6].enable_reg == 0x4603cu &&
			ng.dplls[6].funcs == PARITY_DPLL_FUNCS_DKL &&
			ng.dplls[0].funcs == PARITY_DPLL_FUNCS_COMBO,
			"p5a: P5A-DPLL adlp_plls = DPLL0/1 + TBT + TC1..4 with the reference ids");
		{
			struct parity_display_nogem t;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			parity_intel_shared_dpll_init(&t, 14, 0);
			KCHECK(t.dpll_mgr_present == 0 && t.num_dplls == 0u,
				"p5a: P5A-DPLL ver>=14 has no shared DPLLs (no table is invented)");
		}

		/* ---- P5A-CRTC: 6 planes per pipe (1 primary + 4 sprites + cursor) ---- */
		for (i = 0u; i < sizeof(ng); i++) ((char *)&ng)[i] = 0;
		KCHECK(parity_intel_crtc_init(&ng, 13, 0u) == 0 &&
			ng.crtcs[0].num_planes == 6u && ng.crtcs[0].num_scalers == 2u &&
			ng.crtcs[0].planes[0].type == PARITY_PLANE_PRIMARY &&
			ng.crtcs[0].planes[1].type == PARITY_PLANE_SPRITE &&
			ng.crtcs[0].planes[4].type == PARITY_PLANE_SPRITE &&
			ng.crtcs[0].planes[5].type == PARITY_PLANE_CURSOR &&
			ng.crtcs[0].planes[5].id == 7 &&
			ng.crtcs[0].plane_ids_mask == 0x9fu &&   /* ids 0,1,2,3,4,7 */
			ng.crtcs[0].state.cpu_transcoder == -1 &&
			ng.crtcs[0].fifo_underrun_reporting == 0,
			"p5a: P5A-CRTC ADL-P pipe = primary + 4 sprites + cursor, 2 scalers");

		/* ---- P5A-MAXCDCLK: the ver>=11 ref-clock split ---- */
		for (i = 0u; i < sizeof(ng); i++) ((char *)&ng)[i] = 0;
		parity_intel_update_max_cdclk(&ng, 13, 38400u);
		KCHECK(ng.max_cdclk_freq == 652800u, "p5a: P5A-MAXCDCLK ref 38.4MHz -> 652800");
		parity_intel_update_max_cdclk(&ng, 13, 24000u);
		KCHECK(ng.max_cdclk_freq == 648000u, "p5a: P5A-MAXCDCLK ref 24MHz -> 648000");

		/* ---- P5A-WA: the two ADL-P display WA register operations ---- */
		fake_mmio_open(&m, &f);
		for (i = 0u; i < sizeof(ng); i++) ((char *)&ng)[i] = 0;
		osdep_mmio_raw_write32(&m, 0x46540u, 0u);
		osdep_mmio_raw_write32(&m, 0x46430u, 0xffffffffu);
		f.wt_n = 0u;
		parity_adlp_display_wa_apply(&ng, &m, 13, 1);
		KCHECK(ng.adlp_wa_applied == 1 &&
			fake_wt_find(&f, 0x46540u, (1u << 17), (1u << 17)) >= 0 &&  /* DPCE_GATING_DIS set */
			fake_wt_find(&f, 0x46430u, 0u, (1u << 7)) >= 0,             /* DDI_CLOCK_REG_ACCESS clear */
			"p5a: P5A-WA Wa_22011091694 sets DPCE_GATING_DIS, Bspec49189 clears DDI_CLOCK_REG_ACCESS");
		{
			struct parity_display_nogem t;

			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			f.wt_n = 0u;
			parity_adlp_display_wa_apply(&t, &m, 13, 0 /* not ADL-P */);
			KCHECK(t.adlp_wa_applied == 0 && f.wt_n == 0u,
				"p5a: P5A-WA a non-ADL-P platform writes nothing here");
		}

		/* ---- P5B-PORTMAP: the xelpd DVO->port mapping is NOT the legacy one ---- */
		KCHECK(parity_dvo_port_to_port(13, 0u)  == PARITY_PORT_A &&   /* HDMIA */
			parity_dvo_port_to_port(13, 1u)  == PARITY_PORT_B &&   /* HDMIB */
			parity_dvo_port_to_port(13, 2u)  == PARITY_PORT_C &&   /* HDMIC */
			parity_dvo_port_to_port(13, 14u) == PARITY_PORT_TC1 && /* HDMIF */
			parity_dvo_port_to_port(13, 17u) == PARITY_PORT_TC4 && /* HDMII */
			parity_dvo_port_to_port(13, 10u) == PARITY_PORT_A &&   /* DPA */
			parity_dvo_port_to_port(13, 3u)  == PARITY_PORT_NONE &&/* HDMID: not xelpd */
			parity_dvo_port_to_port(12, 3u)  == PARITY_PORT_D,     /* legacy map */
			"p5b: P5B-PORTMAP ver>=13 uses the xelpd map (TC ports take HDMIF..HDMII)");
		KCHECK(parity_intel_port_to_phy(13, PARITY_PORT_A) == PARITY_PHY_A &&
			parity_intel_port_to_phy(13, PARITY_PORT_B) == PARITY_PHY_B &&
			parity_intel_port_to_phy(13, PARITY_PORT_TC1) == PARITY_PHY_F &&
			parity_intel_phy_is_tc(13, PARITY_PHY_B) == 0 &&
			parity_intel_phy_is_tc(13, PARITY_PHY_F) == 1 &&
			parity_intel_ddi_is_tc(13, PARITY_PORT_B) == 0 &&
			parity_intel_ddi_is_tc(13, PARITY_PORT_TC1) == 1 &&
			parity_intel_ddi_crt_present(13) == 0,
			"p5b: P5B-PORTMAP port->phy, phy_is_tc, ddi_is_tc, no DDI CRT on ver>=9");

		/* ---- P5B-OUTPUTS: the missing-defaults VBT on ADL-P ---- */
		{
			static struct parity_vbt_state vb;
			struct parity_display_nogem t;
			unsigned pm = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) |
				      (1u << 5) | (1u << 6);   /* A,B,TC1..TC4 */

			for (i = 0u; i < sizeof(vb); i++) ((char *)&vb)[i] = 0;
			parity_bios_init_vbt_missing_defaults(&vb);
			for (i = 0u; i < sizeof(t); i++) ((char *)&t)[i] = 0;
			fake_mmio_open(&m, &f);
			parity_intel_setup_outputs(&t, 13, pm, &vb, &m);
			/*
			 * init_vbt_missing_defaults() generates children for PORT_A/B/C
			 * (the non-TC PHYs).  On ADL-P the port_mask has no PORT_C, so the
			 * third child legitimately fails assert_port_valid and is skipped:
			 * TWO encoders, not three.
			 */
			KCHECK(vb.num_display_devices == 3u && t.ddi_init_calls == 3u &&
				t.num_encoders == 2u && t.ddi_skipped == 1u &&
				t.ddi_skip_reason[0] == PARITY_DDI_SKIP_PORT_INVALID &&
				t.ddi_skip_port[0] == PARITY_PORT_C &&
				t.encoders[0].port == PARITY_PORT_A &&
				t.encoders[1].port == PARITY_PORT_B &&
				t.outputs_done == 1 && t.crt_present == 0,
				"p5b: P5B-OUTPUTS 3 VBT children -> 2 encoders; PORT_C fails assert_port_valid");
			KCHECK(t.encoders[0].phy == PARITY_PHY_A && t.encoders[0].is_tc == 0 &&
				t.encoders[0].clk_funcs == PARITY_DDI_CLK_ICL_COMBO &&
				t.encoders[0].power_domain == 17 &&    /* DDI_LANES_A */
				t.encoders[0].init_dp == 1 && t.encoders[0].init_hdmi == 0 &&
				t.encoders[1].power_domain == 18 &&    /* DDI_LANES_B */
				t.encoders[1].init_dp == 1 && t.encoders[1].init_hdmi == 1,
				"p5b: P5B-OUTPUTS PORT_A is eDP-only (no DVI bit), PORT_B is DP+HDMI");

			/* Each early return of intel_ddi_init is reachable. */
			{
				struct parity_vbt_state v2;
				struct parity_display_nogem t2;
				unsigned ok = 1u;

				for (i = 0u; i < sizeof(v2); i++) ((char *)&v2)[i] = 0;
				v2.num_display_devices = 1u;

				/* dvo_port that maps to no port at all */
				v2.display_devices[0].dvo_port = 99u;
				v2.display_devices[0].device_type = 0x4u;
				for (i = 0u; i < sizeof(t2); i++) ((char *)&t2)[i] = 0;
				parity_intel_setup_outputs(&t2, 13, pm, &v2, &m);
				if (t2.num_encoders != 0u ||
				    t2.ddi_skip_reason[0] != PARITY_DDI_SKIP_PORT_NONE) ok = 0u;

				/* DSI child -> the icl_dsi_init path, not intel_ddi_init */
				v2.display_devices[0].dvo_port = 0u;          /* HDMIA -> PORT_A */
				v2.display_devices[0].device_type = (1u << 10);   /* MIPI_OUTPUT */
				for (i = 0u; i < sizeof(t2); i++) ((char *)&t2)[i] = 0;
				parity_intel_setup_outputs(&t2, 13, pm, &v2, &m);
				if (t2.num_encoders != 0u ||
				    t2.ddi_skip_reason[0] != PARITY_DDI_SKIP_DSI) ok = 0u;

				/* neither DVI/HDMI nor DP -> "respect it" */
				v2.display_devices[0].device_type = 0u;
				for (i = 0u; i < sizeof(t2); i++) ((char *)&t2)[i] = 0;
				parity_intel_setup_outputs(&t2, 13, pm, &v2, &m);
				if (t2.num_encoders != 0u ||
				    t2.ddi_skip_reason[0] != PARITY_DDI_SKIP_NOT_DVI_HDMI_DP) ok = 0u;

				/* the same port twice -> already claimed */
				v2.num_display_devices = 2u;
				v2.display_devices[0].dvo_port = 0u;
				v2.display_devices[0].device_type = 0x4u;
				v2.display_devices[1].dvo_port = 10u;   /* DPA -> PORT_A too */
				v2.display_devices[1].device_type = 0x4u;
				for (i = 0u; i < sizeof(t2); i++) ((char *)&t2)[i] = 0;
				parity_intel_setup_outputs(&t2, 13, pm, &v2, &m);
				if (t2.num_encoders != 1u ||
				    t2.ddi_skip_reason[0] != PARITY_DDI_SKIP_PORT_IN_USE) ok = 0u;

				KCHECK(ok == 1u,
					"p5b: P5B-OUTPUTS every intel_ddi_init early return is reachable");
			}

			/* ---- P5B-DDICLK: combo vs TC clock state, and the disable ---- */
			{
				struct parity_encoder combo, tc;
				unsigned ok = 1u;

				for (i = 0u; i < sizeof(combo); i++) ((char *)&combo)[i] = 0;
				combo.port = PARITY_PORT_B; combo.phy = PARITY_PHY_B;
				combo.clk_funcs = PARITY_DDI_CLK_ICL_COMBO;
				/* DDI_CLK_OFF(PHY_B) is bit 11 */
				osdep_mmio_raw_write32(&m, 0x164280u, 0u);
				if (parity_intel_ddi_is_clock_enabled(&combo, &m) != 1) ok = 0u;
				osdep_mmio_raw_write32(&m, 0x164280u, (1u << 11));
				if (parity_intel_ddi_is_clock_enabled(&combo, &m) != 0) ok = 0u;

				osdep_mmio_raw_write32(&m, 0x164280u, 0u);
				f.wt_n = 0u;
				parity_intel_ddi_disable_clock(&t, &combo, &m);
				if (fake_wt_find(&f, 0x164280u, (1u << 11), (1u << 11)) < 0) ok = 0u;

				/* TC: needs BOTH a non-NONE DDI_CLK_SEL and the TC clk not off */
				for (i = 0u; i < sizeof(tc); i++) ((char *)&tc)[i] = 0;
				tc.port = PARITY_PORT_TC1; tc.phy = PARITY_PHY_F;
				tc.clk_funcs = PARITY_DDI_CLK_ICL_TC;
				osdep_mmio_raw_write32(&m, 0x46100u + 3u * 4u, 0u);  /* SEL_NONE */
				osdep_mmio_raw_write32(&m, 0x164280u, 0u);
				if (parity_intel_ddi_is_clock_enabled(&tc, &m) != 0) ok = 0u;
				osdep_mmio_raw_write32(&m, 0x46100u + 3u * 4u, 0x80000000u);
				if (parity_intel_ddi_is_clock_enabled(&tc, &m) != 1) ok = 0u;
				osdep_mmio_raw_write32(&m, 0x164280u, (1u << 12));   /* TC1 off */
				if (parity_intel_ddi_is_clock_enabled(&tc, &m) != 0) ok = 0u;

				KCHECK(ok == 1u,
					"p5b: P5B-DDICLK combo uses DDI_CLK_OFF(phy); TC needs CLK_SEL + TC_CLK_OFF");
			}
		}

		/* ---- P5C-READOUT: quiescent vs one active pipe ---- */
		{
			static struct parity_display_nogem rd;
			static struct parity_power_domains rpd;
			static struct parity_vbt_state rvb;
			struct parity_pw_ctx rpwc;
			struct osdep_trace *rtr = &ktest_trace_pool[1];   /* 32 KiB ring: shared static, never on the stack */
			unsigned k;
			unsigned pm = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) |
				      (1u << 5) | (1u << 6);

			osdep_trace_init(rtr);
			fake_mmio_open(&m, &f);
			f.fuse_status = 0xFFFFFFFFu;
			(void)parity_intel_power_domains_init(&rpd, 13u, -1, 1, rtr);
			for (i = 0u; i < sizeof(rpwc); i++) ((char *)&rpwc)[i] = 0;
			rpwc.mmio = &m;
			for (k = 0u; k < rpd.num_power_wells; k++)
				rpd.power_wells[k].hw_enabled = 1;

			for (i = 0u; i < sizeof(rd); i++) ((char *)&rd)[i] = 0;
			for (k = 0u; k < 4u; k++) (void)parity_intel_crtc_init(&rd, 13, k);
			parity_intel_shared_dpll_init(&rd, 13, 1);
			for (i = 0u; i < sizeof(rvb); i++) ((char *)&rvb)[i] = 0;
			parity_bios_init_vbt_missing_defaults(&rvb);
			parity_intel_setup_outputs(&rd, 13, pm, &rvb, &m);

			/* (a) everything quiescent -- the real device's state. */
			parity_intel_modeset_readout_hw_state(&rd, 13, &m, &rpd, &rpwc);
			KCHECK(rd.readout_done == 1 && rd.readout_crtcs == 4u &&
				rd.active_pipes == 0u && rd.readout_planes_visible == 0u &&
				rd.readout_encoders_linked == 0u && rd.readout_dplls_on == 0u &&
				rd.crtcs[0].state.cpu_transcoder == -1 &&
				rd.crtcs[0].active == 0 && rd.crtcs[0].enabled == 0,
				"p5c: P5C-READOUT a quiescent device reads back nothing active");

			/*
			 * (b) pipe B active: TRANS_DDI_FUNC_CTL(B) enabled and selecting
			 * DDI B, TRANSCONF(B) enabled, timings + PIPESRC set, PLANE_CTL
			 * (primary B) enabled, DDI_BUF_CTL(B) enabled, DPLL0 on.
			 */
			osdep_mmio_raw_write32(&m, 0x61400u,
				(1u << 31) | ((1u + 1u) << 27));   /* FUNC_ENABLE | port B sel */
			osdep_mmio_raw_write32(&m, 0x71008u, (1u << 31));          /* TRANSCONF B */
			osdep_mmio_raw_write32(&m, 0x61000u, (2559u << 16) | 1919u); /* HTOTAL */
			osdep_mmio_raw_write32(&m, 0x6100cu, (1124u << 16) | 1079u); /* VTOTAL */
			osdep_mmio_raw_write32(&m, 0x6101cu, (1919u << 16) | 1079u); /* PIPESRC */
			osdep_mmio_raw_write32(&m, 0x71180u, (1u << 31));          /* PLANE_CTL B1 */
			osdep_mmio_raw_write32(&m, 0x64100u, (1u << 31));          /* DDI_BUF_CTL B */
			osdep_mmio_raw_write32(&m, 0x46010u, (1u << 31));          /* DPLL0 on */

			parity_intel_modeset_readout_hw_state(&rd, 13, &m, &rpd, &rpwc);
			KCHECK(rd.active_pipes == (1u << 1) && rd.crtcs[1].active == 1 &&
				rd.crtcs[1].enabled == 1 &&
				rd.crtcs[1].state.cpu_transcoder == 1 &&
				rd.crtcs[1].state.enabled_transcoders == (1u << 1) &&
				rd.crtcs[0].active == 0 && rd.crtcs[2].active == 0,
				"p5c: P5C-READOUT pipe B reads back active via TRANS_DDI_FUNC + TRANSCONF");
			KCHECK(rd.crtcs[1].state.hdisplay == 1920u &&
				rd.crtcs[1].state.htotal == 2560u &&
				rd.crtcs[1].state.vdisplay == 1080u &&
				rd.crtcs[1].state.vtotal == 1125u &&
				rd.crtcs[1].state.pipe_src_w == 1920u &&
				rd.crtcs[1].state.pipe_src_h == 1080u,
				"p5c: P5C-READOUT transcoder timings and PIPESRC decode (+1 on each field)");
			KCHECK(rd.readout_planes_visible == 1u &&
				rd.crtcs[1].planes[0].visible == 1 &&
				rd.crtcs[1].state.active_planes == 0x1u &&
				rd.crtcs[0].planes[0].visible == 0,
				"p5c: P5C-READOUT only the enabled plane reads back visible");
			KCHECK(rd.readout_encoders_linked == 1u &&
				rd.encoders[1].crtc_linked == 1 &&
				rd.encoders[1].pipe_mask == (1u << 1) &&
				rd.encoders[1].is_mst == 0 &&
				rd.encoders[0].crtc_linked == 0 &&
				rd.readout_dplls_on == 1u && rd.dplls[0].on == 1 &&
				rd.dplls[1].on == 0,
				"p5c: P5C-READOUT DDI B links to pipe B; DPLL0 reads back on");

			/* (c) the pipe power being off must not be read as "inactive". */
			for (k = 0u; k < rpd.num_power_wells; k++)
				rpd.power_wells[k].hw_enabled =
					rpd.power_wells[k].always_on ? 1 : 0;
			parity_intel_modeset_readout_hw_state(&rd, 13, &m, &rpd, &rpwc);
			KCHECK(rd.active_pipes == 0u && rd.crtcs[1].state.power_gated == 1 &&
				rd.crtcs[1].state.transconf == 0u,
				"p5c: P5C-READOUT an unpowered pipe is recorded as power-gated, not read");
			for (k = 0u; k < rpd.num_power_wells; k++)
				rpd.power_wells[k].hw_enabled = 1;

			/* ---- P5D-SANITIZE: a quiescent device -> every arm is a no-op ---- */
			parity_intel_modeset_readout_hw_state(&rd, 13, &m, &rpd, &rpwc);
			{
				struct parity_display_nogem q;

				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				/* copy just the shape the sanitize needs from the quiet readout */
				for (k = 0u; k < 4u; k++) (void)parity_intel_crtc_init(&q, 13, k);
				parity_intel_shared_dpll_init(&q, 13, 1);
				f.wt_n = 0u;
				parity_intel_modeset_sanitize_hw_state(&q, 13,
					13 /* PARITY_STEP_D0 */, 0x1u, &m, &rpd, &rpwc);
				KCHECK(q.sanitize_done == 1 && q.vblank_resets == 4u &&
					q.dmc_pipes_enabled == 0u && q.vblank_on_count == 0u &&
					q.dplls_disabled == 0u && q.encoder_clocks_gated == 0u &&
					q.crtc_disable_noatomic_unimplemented == 0 &&
					q.cmtg_wa_applied == 0 &&      /* D0, not A0 */
					q.early_display_was_applied == 0 &&  /* ver 13, not 10..12 */
					q.plane_mapping_sanitized == 0,      /* ver >= 4 returns */
					"p5d: P5D-SANITIZE a quiescent device needs no hardware change");
			}

			/* ---- P5D-DPLL: an ON but unused PLL is disabled; an in-use one is not ---- */
			{
				struct parity_display_nogem q;

				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				parity_intel_shared_dpll_init(&q, 13, 1);
				q.dplls[0].on = 1; q.dplls[0].active_mask = 0u;   /* unused */
				q.dplls[1].on = 1; q.dplls[1].active_mask = 0x2u; /* in use */
				q.dplls[2].on = 0;
				osdep_mmio_raw_write32(&m, 0x46010u, (1u << 31));
				osdep_mmio_raw_write32(&m, 0x46014u, (1u << 31));
				f.wt_n = 0u;
				parity_intel_modeset_sanitize_hw_state(&q, 13, 13, 0u, &m,
					&rpd, &rpwc);
				KCHECK(q.dplls_disabled == 1u && q.dplls[0].on == 0 &&
					q.dplls[1].on == 1 &&
					fake_wt_find(&f, 0x46010u, 0u, (1u << 31)) >= 0 &&
					fake_wt_find(&f, 0x46014u, 0u, (1u << 31)) < 0,
					"p5d: P5D-DPLL only an enabled-but-unused shared DPLL is turned off");
			}

			/* ---- P5D-CMTG: the ADL-P A0 CMTG WA gate ---- */
			{
				struct parity_display_nogem q;

				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				parity_intel_shared_dpll_init(&q, 13, 1);
				q.dplls[0].on = 1; q.dplls[0].active_mask = 0x1u;   /* in use */
				q.dplls[1].on = 1; q.dplls[1].active_mask = 0x1u;
				parity_intel_modeset_sanitize_hw_state(&q, 13,
					1 /* PARITY_STEP_A0 */, 0u, &m, &rpd, &rpwc);
				KCHECK(q.cmtg_wa_applied == 1 && q.dplls_disabled == 0u,
					"p5d: P5D-CMTG Wa_16011069516 fires on ADL-P A0 for DPLL0 only");
			}

			/* ---- P5D-FBC: an FBC left active by the pre-OS is deactivated ---- */
			{
				struct parity_display_nogem q;

				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				osdep_mmio_raw_write32(&m, 0x43208u, (1u << 31) | 0x5u);
				f.wt_n = 0u;
				parity_intel_modeset_sanitize_hw_state(&q, 13, 13, 0x1u, &m,
					&rpd, &rpwc);
				KCHECK(q.fbc_deactivated == 1u &&
					fake_wt_find(&f, 0x43208u, 0x5u, 0xffffffffu) >= 0,
					"p5d: P5D-FBC an active FBC is deactivated by clearing DPFC_CTL_EN only");

				osdep_mmio_raw_write32(&m, 0x43208u, 0u);
				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				f.wt_n = 0u;
				parity_intel_modeset_sanitize_hw_state(&q, 13, 13, 0x1u, &m,
					&rpd, &rpwc);
				KCHECK(q.fbc_deactivated == 0u &&
					fake_wt_find(&f, 0x43208u, 0u, 0xffffffffu) < 0,
					"p5d: P5D-FBC an inactive FBC is left alone");
			}

			/* ---- P5D-ENCCLK: a disabled encoder with an ungated DDI clock ---- */
			{
				struct parity_display_nogem q;

				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				q.num_encoders = 2u;
				q.encoders[0].port = PARITY_PORT_A; q.encoders[0].phy = PARITY_PHY_A;
				q.encoders[0].clk_funcs = PARITY_DDI_CLK_ICL_COMBO;
				q.encoders[0].crtc_linked = 0;          /* disabled */
				q.encoders[1].port = PARITY_PORT_B; q.encoders[1].phy = PARITY_PHY_B;
				q.encoders[1].clk_funcs = PARITY_DDI_CLK_ICL_COMBO;
				q.encoders[1].crtc_linked = 1;          /* in use -> left alone */
				osdep_mmio_raw_write32(&m, 0x164280u, 0u);   /* both clocks ungated */
				f.wt_n = 0u;
				parity_intel_modeset_sanitize_hw_state(&q, 13, 13, 0u, &m,
					&rpd, &rpwc);
				KCHECK(q.encoder_clocks_gated == 1u &&
					fake_wt_find(&f, 0x164280u, (1u << 10), (1u << 10)) >= 0 &&
					fake_wt_find(&f, 0x164280u, (1u << 11), (1u << 11)) < 0,
					"p5d: P5D-ENCCLK only the DISABLED encoder's DDI clock is gated");
			}

			/* ---- P5D-ACTIVE: an active pipe with no encoders is reported, not faked ---- */
			{
				struct parity_display_nogem q;

				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				(void)parity_intel_crtc_init(&q, 13, 1u);
				q.crtcs[1].state.active = 1;
				f.wt_n = 0u;
				parity_intel_modeset_sanitize_hw_state(&q, 13, 13, 0u, &m,
					&rpd, &rpwc);
				KCHECK(q.crtc_disable_noatomic_unimplemented == 1 &&
					q.dmc_pipes_enabled == 1u && q.vblank_on_count == 1u &&
					q.crtcs[1].fifo_underrun_reporting == 0 &&
					fake_wt_find(&f, 0x45250u + 4u, (1u << 0), (1u << 0)) >= 0,
					"p5d: P5D-ACTIVE an active pipe enables its DMC + vblank and flags "
					"the missing crtc_disable_noatomic instead of pretending");
			}

			/* ---- P5D-WELL: a BIOS-left unused well is disabled, in reverse ---- */
			{
				struct parity_display_nogem q;
				unsigned before = 0u, after = 0u;

				/*
				 * The DC_off well's disable (P7) enables the target DC state
				 * only with a DMC payload; give the sanitize one so that well
				 * can actually turn off too.  Counts use the same fresh
				 * is_enabled() read the sanitize uses.
				 */
				rpwc.display_ver = 13;
				rpwc.dmc_has_payload = 1;
				rpwc.allowed_dc_mask = rpd.allowed_dc_mask;
				rpwc.target_dc_state = rpd.target_dc_state;
				for (k = 0u; k < rpd.num_power_wells; k++)
					if (parity_power_well_is_enabled(&rpd.power_wells[k], &rpwc)) before++;
				for (i = 0u; i < sizeof(q); i++) ((char *)&q)[i] = 0;
				parity_intel_modeset_sanitize_hw_state(&q, 13, 13, 0u, &m,
					&rpd, &rpwc);
				for (k = 0u; k < rpd.num_power_wells; k++)
					if (parity_power_well_is_enabled(&rpd.power_wells[k], &rpwc)) after++;
				KCHECK(before > 0u && q.wells_disabled + after == before,
					"p5d: P5D-WELL every unreferenced non-always-on well that reads "
					"back enabled is disabled");
			}

			parity_intel_power_domains_cleanup(&rpd);
		}

		/* ---- P5A-VGA: the disable sequence is IO-first, then the register ---- */
		{
			int rc;

			fake_mmio_open(&m, &f);
			/* already disabled -> the reference returns without touching IO */
			osdep_mmio_raw_write32(&m, 0x41000u, (1u << 31));
			g_vga_rec.n = 0u; g_vga_rec.got = 0; g_vga_rec.put = 0;
			parity_vga_io_test_set(&vga_rec_ops);
			rc = parity_intel_vga_disable(0, &m);
			KCHECK(rc == 1 && g_vga_rec.n == 0u,
				"p5a: P5A-VGA VGA_DISP_DISABLE already set -> no IO, no write");

			/* not disabled -> get, SR01 read-modify-write, put, then the reg */
			osdep_mmio_raw_write32(&m, 0x41000u, 0u);
			g_vga_rec.n = 0u; g_vga_rec.got = 0; g_vga_rec.put = 0;
			f.wt_n = 0u;
			rc = parity_intel_vga_disable(0, &m);
			parity_vga_io_test_set(0);
			KCHECK(rc == 0 && g_vga_rec.got == 1 && g_vga_rec.put == 1 &&
				g_vga_rec.n == 5u &&               /* get, out, in, out, put */
				g_vga_rec.seq[0] == 1 && g_vga_rec.seq[1] == 3 &&
				g_vga_rec.seq[2] == 2 && g_vga_rec.seq[3] == 3 &&
				g_vga_rec.seq[4] == 4 &&
				(g_vga_rec.last_written & 0x20u) != 0u &&   /* SR01 screen-off */
				fake_wt_find(&f, 0x41000u, (1u << 31), (1u << 31)) >= 0,
				"p5a: P5A-VGA legacy IO screen-off first, then VGA_DISP_DISABLE");
		}
	}

	/* --- P6-0: the rest of intel_gt_init_mmio + the GT irq ack path --- */
	{
		static struct parity_gt_mmio gm;
		static struct parity_irq_dev gid;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		unsigned i;

		/* ---- P60-SSEU: the EU_DISABLE pair expansion ---- */
		fake_mmio_open(&m, &f);
		osdep_mmio_raw_write32(&m, 0x9138u, 0x1u);        /* GT_SLICE_ENABLE = 1 */
		osdep_mmio_raw_write32(&m, 0x913cu, 0x3fu);       /* GEOMETRY_DSS_ENABLE: 6 DSS */
		osdep_mmio_raw_write32(&m, 0x9134u, 0x00u);       /* EU_DISABLE = 0 -> all on */
		for (i = 0u; i < sizeof(gm); i++) ((char *)&gm)[i] = 0;
		parity_gen12_sseu_info_init(&gm.sseu, &m);
		KCHECK(gm.sseu.max_slices == 1u && gm.sseu.max_subslices == 6u &&
			gm.sseu.max_eus_per_subslice == 16u &&
			gm.sseu.slice_mask == 0x1u && gm.sseu.subslice_mask == 0x3fu &&
			gm.sseu.eu_mask[0] == 0xffffu &&
			gm.sseu.eu_per_subslice == 16u &&
			gm.sseu.eu_total == 96u &&        /* 6 DSS x 16 EU */
			gm.sseu.has_slice_pg == 1,
			"p60: P60-SSEU gen12 = 1 slice, 6 DSS, 16 EU/DSS, 96 EUs when nothing is fused off");
		/*
		 * EU_DISABLE has ONE BIT PER PAIR: disabling bit0 must remove TWO
		 * EUs, not one.  A naive 1-bit-per-EU decode passes the test above
		 * and fails here.
		 */
		osdep_mmio_raw_write32(&m, 0x9134u, 0x03u);       /* pairs 0,1 disabled */
		osdep_mmio_raw_write32(&m, 0x913cu, 0x03u);       /* only DSS 0,1 present */
		for (i = 0u; i < sizeof(gm); i++) ((char *)&gm)[i] = 0;
		parity_gen12_sseu_info_init(&gm.sseu, &m);
		KCHECK(gm.sseu.subslice_mask == 0x3u &&
			gm.sseu.eu_mask[0] == 0xfff0u &&   /* EUs 0..3 gone */
			gm.sseu.eu_per_subslice == 12u &&
			gm.sseu.eu_total == 24u &&
			gm.sseu.eu_mask[2] == 0u,          /* absent DSS stay empty */
			"p60: P60-SSEU EU_DISABLE is one bit PER PAIR (2 EUs per set bit)");

		/* ---- P60-CLOCK: CTC_MODE selects crystal vs divide-logic ---- */
		{
			uint32_t period = 0u, freq;

			osdep_mmio_raw_write32(&m, 0xa26cu, 0u);       /* CTC: crystal */
			/* RPM_CONFIG0: 38.4MHz (=2 at shift 3), ctc shift parameter 3 */
			osdep_mmio_raw_write32(&m, 0xd00u, (2u << 3) | (3u << 1));
			freq = parity_gen11_read_clock_frequency(&m, &period);
			KCHECK(freq == 38400000u && period == 26u,   /* >> (3-3) = no shift */
				"p60: P60-CLOCK crystal 38.4MHz, ctc shift 3 -> no divide");
			osdep_mmio_raw_write32(&m, 0xd00u, (2u << 3) | (1u << 1));
			freq = parity_gen11_read_clock_frequency(&m, &period);
			KCHECK(freq == 38400000u >> 2,               /* >> (3-1) */
				"p60: P60-CLOCK the ctc shift parameter divides the timestamp clock");
		}

		/* ---- P60-ENGINES: the media fuses and the engine table ---- */
		fake_mmio_open(&m, &f);
		osdep_mmio_raw_write32(&m, 0x9138u, 0x1u);
		osdep_mmio_raw_write32(&m, 0x913cu, 0x3fu);
		osdep_mmio_raw_write32(&m, 0x9134u, 0u);
		osdep_mmio_raw_write32(&m, 0x9118u, 0u);          /* MIRROR_FUSE3 */
		/*
		 * GEN11_GT_VEBOX_VDBOX_DISABLE has DISABLE semantics and the
		 * reference INVERTS it: writing 0 means "nothing disabled".
		 */
		osdep_mmio_raw_write32(&m, 0x9140u, 0u);
		for (i = 0u; i < sizeof(gm); i++) ((char *)&gm)[i] = 0;
		KCHECK(parity_intel_gt_init_mmio(&gm, 12,
			(1u << 0) | (1u << 1) | (1u << 8) | (1u << 10) | (1u << 16),
			&m) == 0 &&
			gm.num_engines == 5u &&
			/* RCS0(0)|BCS0(1)|VCS0(8)|VCS2(10)|VECS0(16) */
			gm.engine_mask == 0x10503u &&
			gm.engines[0].mmio_base == 0x02000u &&    /* rcs0 */
			gm.engines[1].mmio_base == 0x22000u &&    /* bcs0 */
			gm.engines[2].mmio_base == 0x1c0000u &&   /* vcs0 */
			gm.engines[3].mmio_base == 0x1d0000u &&   /* vcs2 = BSD3, not BSD2 */
			gm.engines[4].mmio_base == 0x1c8000u &&   /* vecs0 */
			gm.engines[0].context_size == 14u * 4096u &&
			gm.engines[1].context_size == 2u * 4096u &&
			gm.l3bank_mask == 0xfu,
			"p60: P60-ENGINES ADL-P builds rcs0/bcs0/vcs0/vcs2/vecs0 with the reference bases");
		/* A fused-off VCS2 must drop that engine (and only that one). */
		osdep_mmio_raw_write32(&m, 0x9140u, (1u << 2));   /* disable vdbox 2 */
		for (i = 0u; i < sizeof(gm); i++) ((char *)&gm)[i] = 0;
		(void)parity_intel_gt_init_mmio(&gm, 12,
			(1u << 0) | (1u << 1) | (1u << 8) | (1u << 10) | (1u << 16), &m);
		KCHECK(gm.num_engines == 4u && (gm.engine_mask & (1u << 10)) == 0u &&
			(gm.engine_mask & (1u << 8)) != 0u,
			"p60: P60-ENGINES a fused-off VDBOX removes exactly that engine");

		/* ---- P60-FAULT: a valid fault is reported and cleared ---- */
		fake_mmio_open(&m, &f);
		osdep_mmio_raw_write32(&m, 0xcec4u, 0x1u | (3u << 12));
		for (i = 0u; i < sizeof(gm); i++) ((char *)&gm)[i] = 0;
		f.wt_n = 0u;
		parity_intel_gt_check_and_clear_faults(&gm, &m);
		/*
		 * intel_gt_clear_error_registers() clears RING_FAULT_VALID with a
		 * read-modify-write: the other fields (engine id, address bits)
		 * are preserved, so the write is 0x3000, not 0.
		 */
		KCHECK(gm.fault_valid_seen == 1 &&
			fake_wt_find(&f, 0xcec4u, 3u << 12, 0xffffffffu) >= 0,
			"p60: P60-FAULT a valid ring fault is decoded and only VALID is cleared");

		/* ---- P60-GTIRQ: the identity handshake and the engine decode ---- */
		fake_mmio_open(&m, &f);
		osdep_mmio_raw_write32(&m, 0x9138u, 0x1u);
		osdep_mmio_raw_write32(&m, 0x913cu, 0x3fu);
		osdep_mmio_raw_write32(&m, 0x9134u, 0u);
		osdep_mmio_raw_write32(&m, 0x9140u, 0u);
		for (i = 0u; i < sizeof(gm); i++) ((char *)&gm)[i] = 0;
		(void)parity_intel_gt_init_mmio(&gm, 12,
			(1u << 0) | (1u << 1) | (1u << 8) | (1u << 10) | (1u << 16), &m);

		for (i = 0u; i < sizeof(gid); i++) ((char *)&gid)[i] = 0;
		gid.m = &m; gid.gt = &gm; gid.display_ver = 13; gid.pipe_mask = 0xfu;
		/* bank 0 bit 0 asserted; identity = RCS0 (class 0, inst 0) with
		 * USER | CONTEXT_SWITCH set. */
		osdep_mmio_raw_write32(&m, 0x190018u, 0x1u);
		osdep_mmio_raw_write32(&m, 0x190060u,
			(1u << 31) | (0u << 16) | (0u << 20) | 0x101u);
		f.wt_n = 0u;
		parity_gen11_gt_irq_handler(&gid, 0x1u);   /* GEN11_GT_DW_IRQ(0) */
		KCHECK(gid.gt_identity_reads == 1u && gid.gt_identity_invalid == 0u &&
			gid.gt_engine_intrs == 1u && gid.gt_user_intr == 1u &&
			gid.gt_ctx_switch_intr == 1u && gid.gt_error_intr == 0u &&
			gid.gt_bank_acks[0] == 1u && gid.gt_unknown_class == 0u,
			"p60: P60-GTIRQ identity -> RCS0, USER + CTX_SWITCH decoded");
		KCHECK(fake_wt_find(&f, 0x190070u, 0x1u, 0xffffffffu) == 0 &&   /* selector first */
			fake_wt_find(&f, 0x190060u, (1u << 31), (1u << 31)) > 0 &&  /* identity ack */
			fake_wt_find(&f, 0x190018u, 0x1u, 0xffffffffu) > 1,         /* bank clear LAST */
			"p60: P60-GTIRQ selector -> identity ack -> INTR_DW clear, in that order");

		/* An identity that never validates is reported, not treated as an engine. */
		osdep_mmio_raw_write32(&m, 0x190018u, 0x1u);
		osdep_mmio_raw_write32(&m, 0x190060u, 0u);   /* DATA_VALID never set */
		gid.gt_engine_intrs = 0u; gid.gt_identity_invalid = 0u;
		parity_gen11_gt_irq_handler(&gid, 0x1u);
		KCHECK(gid.gt_identity_invalid == 1u && gid.gt_engine_intrs == 0u,
			"p60: P60-GTIRQ an identity that never reports DATA_VALID is counted, not decoded");

		/* OTHER_CLASS (GuC/GTPM) is counted separately, not "unknown". */
		osdep_mmio_raw_write32(&m, 0x190018u, 0x2u);
		osdep_mmio_raw_write32(&m, 0x190060u,
			(1u << 31) | (4u << 16) | (1u << 20) | 0x4u);   /* OTHER/GTPM */
		gid.gt_other_intrs = 0u; gid.gt_unknown_class = 0u;
		parity_gen11_gt_irq_handler(&gid, 0x1u);
		KCHECK(gid.gt_other_intrs == 1u && gid.gt_unknown_class == 0u,
			"p60: P60-GTIRQ OTHER_CLASS is accounted separately from unknown classes");
	}

	/* --- P6-a / P6-b: the ADL-P workaround manifest, MOCS, PAT, RC6/RPS --- */
	{
		static struct parity_gt_mmio wm_gt;
		static struct parity_gt_init wg;
		static struct osdep_mmio m;   /* large: keep off the 16 KiB stack */
		static struct fake_mmio_state f;   /* large: keep off the 16 KiB stack */
		struct mutex wsb;
		unsigned i;
		int rcs = -1, bcs = -1;

		(void)mutex_init(&wsb, LOCK_RANK_DEVICE, "ktest-gtwa");
		fake_mmio_open(&m, &f);
		osdep_mmio_raw_write32(&m, 0x9138u, 0x1u);
		osdep_mmio_raw_write32(&m, 0x913cu, 0x3eu);   /* DSS 1..5: lowest is 1 */
		osdep_mmio_raw_write32(&m, 0x9134u, 0u);
		osdep_mmio_raw_write32(&m, 0x9140u, 0u);
		for (i = 0u; i < sizeof(wm_gt); i++) ((char *)&wm_gt)[i] = 0;
		(void)parity_intel_gt_init_mmio(&wm_gt, 12,
			(1u << 0) | (1u << 1) | (1u << 8) | (1u << 10) | (1u << 16), &m);
		for (i = 0u; i < wm_gt.num_engines; i++) {
			if (wm_gt.engines[i].class == PARITY_RENDER_CLASS) rcs = (int)i;
			if (wm_gt.engines[i].class == PARITY_COPY_ENGINE_CLASS) bcs = (int)i;
		}

		/* ---- P6A-GTWA: gen12_gt_workarounds_init for ADL-P ---- */
		for (i = 0u; i < sizeof(wg); i++) ((char *)&wg)[i] = 0;
		parity_gt_init_workarounds_adlp(&wg.gt_wa, &wm_gt);
		{
			unsigned found = 0u;
			int mcr_ok = 0, dfr_ok = 0, misc_ok = 0, vd0 = 0, vd2 = 0;

			for (i = 0u; i < wg.gt_wa.count; i++) {
				const struct parity_wa *w = &wg.gt_wa.list[i];

				found++;
				/* icl_wa_init_mcr: steer at the LOWEST live subslice (1). */
				if (w->reg == 0x0fdcu && w->clr == 0x7f000000u &&
				    w->set == (1u << 24) && w->is_mcr == 0)
					mcr_ok = 1;
				/* Wa_14011059788: MCR, clr == set (wa_write_or). */
				if (w->reg == 0x9550u && w->is_mcr == 1 &&
				    w->clr == 0x200u && w->set == 0x200u &&
				    w->read_mask == 0x200u)
					dfr_ok = 1;
				/*
				 * Wa_14015795083 CLEARS the bit (the reference's arg order
				 * is (reg, clear, set, ...)) and is NOT verified.
				 */
				if (w->reg == 0x9424u && w->clr == 0x2u && w->set == 0u &&
				    w->read_mask == 0u && w->kind == PARITY_WA_NO_VERIFY)
					misc_ok = 1;
				/* Wa_14011060649: even-instance VDBOX only. */
				if (w->reg == 0x1c3f10u && w->set == (1u << 22)) vd0 = 1;
				if (w->reg == 0x1d3f10u && w->set == (1u << 22)) vd2 = 1;
			}
			KCHECK(found == 5u && mcr_ok && dfr_ok && misc_ok && vd0 && vd2,
				"p6a: P6A-GTWA MCR steer + Wa_14011060649 x2 + 14011059788 + 14015795083");
		}

		/* ---- P6A-RCSWA: the ADL-P render engine workarounds ---- */
		KCHECK(rcs >= 0, "p6a: P6A-RCSWA render engine present");
		parity_engine_init_workarounds(&wg.engine_wa[0], &wm_gt.engines[rcs], 12,
			3u /* mocs uc_index */);
		{
			int cctl = 0, dop = 0, row2 = 0, ffthread = 0, smallpl = 0;
			int row4 = 0, psmi = 0, perctx = 0;

			for (i = 0u; i < wg.engine_wa[0].count; i++) {
				const struct parity_wa *w = &wg.engine_wa[0].list[i];

				/* RING_CMD_CCTL MOCS override for uc_index 3 -> 0x306. */
				if (w->reg == 0x20c4u && w->set == ((0x3fffu << 16) | 0x306u))
					cctl = 1;
				if (w->reg == 0x20ecu && w->set == 0x00020002u) dop = 1;
				/*
				 * Wa_1606931601 and Wa_1409804808 target the SAME MCR
				 * register and must MERGE into one entry.
				 */
				if (w->reg == 0xe4f4u && w->is_mcr == 1 &&
				    w->set == 0x41004100u && w->read_mask == 0x4100u)
					row2 = 1;
				if (w->reg == 0x20a0u && w->clr == 0x00080000u &&
				    w->set == 0x00080000u) ffthread = 1;
				if (w->reg == 0xe18cu && w->is_mcr == 1 &&
				    w->set == 0x80008000u) smallpl = 1;
				if (w->reg == 0xe48cu && w->is_mcr == 1 &&
				    w->set == 0x02000200u) row4 = 1;
				if (w->reg == 0x2050u && w->set == 0x10801080u) psmi = 1;
				if (w->reg == 0x20e0u && w->set == 0x40004000u) perctx = 1;
			}
			KCHECK(cctl && dop && row2 && ffthread && smallpl && row4 &&
				psmi && perctx && wg.engine_wa[0].count == 8u,
				"p6a: P6A-RCSWA all eight ADL-P RCS entries, ROW_CHICKEN2 merged");
		}

		/* A non-render engine gets only the fake CMD_CCTL entry. */
		KCHECK(bcs >= 0, "p6a: P6A-RCSWA copy engine present");
		parity_engine_init_workarounds(&wg.engine_wa[1], &wm_gt.engines[bcs], 12, 3u);
		KCHECK(wg.engine_wa[1].count == 1u &&
			wg.engine_wa[1].list[0].reg == 0x220c4u,
			"p6a: P6A-RCSWA a non-render engine only gets the CMD_CCTL fake WA");

		/* ---- P6A-CTXWA: the context workarounds ---- */
		parity_engine_init_ctx_wa(&wg.ctx_wa[0], &wm_gt.engines[rcs], 12, 3u);
		{
			int cps = 0, preempt = 0, ffmode2 = 0, hiz = 0, tdc = 0;

			for (i = 0u; i < wg.ctx_wa[0].count; i++) {
				const struct parity_wa *w = &wg.ctx_wa[0].list[i];

				if (w->reg == 0x7304u && w->set == 0x02000200u) cps = 1;
				if (w->reg == 0x2580u && w->set == 0x00060002u) preempt = 1;
				/* FF_MODE2: clr ~0, set TDS_128|GS_224, NOT verified. */
				if (w->reg == 0x6604u && w->clr == 0xffffffffu &&
				    w->set == (0xe0000000u | 0x00040000u) &&
				    w->kind == PARITY_WA_NO_VERIFY) ffmode2 = 1;
				if (w->reg == 0x7018u && w->set == 0x20002000u) hiz = 1;
				if (w->reg == 0x7300u && w->set == 0x00400040u) tdc = 1;
			}
			KCHECK(cps && preempt && ffmode2 && hiz && tdc &&
				wg.ctx_wa[0].count == 5u,
				"p6a: P6A-CTXWA five gen12 context WAs; FF_MODE2 is write-only");
		}
		/* The copy engine's context WA is BLIT_CCTL, and only that. */
		parity_engine_init_ctx_wa(&wg.ctx_wa[1], &wm_gt.engines[bcs], 12, 3u);
		KCHECK(wg.ctx_wa[1].count == 1u &&
			wg.ctx_wa[1].list[0].reg == 0x22204u &&
			wg.ctx_wa[1].list[0].clr == 0x7f7fu &&
			wg.ctx_wa[1].list[0].set == 0x0606u,
			"p6a: P6A-CTXWA gen12_ctx_gt_mocs_init programs BLIT_CCTL on the copy engine only");

		/* ---- P6A-WHITELIST ---- */
		parity_engine_init_whitelist(&wg.whitelist[0], &wm_gt.engines[rcs], 12);
		KCHECK(wg.whitelist[0].count == 4u &&
			wg.whitelist[0].list[0].reg == (0x2348u | (1u << 28) | 1u) &&
			wg.whitelist[0].list[1].reg == 0x7010u &&
			wg.whitelist[0].list[2].reg == 0x7018u &&
			wg.whitelist[0].list[3].reg == 0x7304u,
			"p6a: P6A-WHITELIST PS_INVOCATION_COUNT (RD, range4) + three RW regs");
		f.wt_n = 0u;
		parity_engine_apply_whitelist(&wg.whitelist[0], &wm_gt.engines[rcs], &m);
		KCHECK(fake_wt_find(&f, 0x24d0u, 0x10002349u, 0xffffffffu) >= 0 &&
			fake_wt_find(&f, 0x24d4u, 0x7010u, 0xffffffffu) >= 0 &&
			fake_wt_find(&f, 0x24dcu, 0x7304u, 0xffffffffu) >= 0 &&
			/* the remaining eight slots are filled with RING_NOPID */
			fake_wt_find(&f, 0x24e0u, 0x2094u, 0xffffffffu) >= 0 &&
			fake_wt_find(&f, 0x24fcu, 0x2094u, 0xffffffffu) >= 0,
			"p6a: P6A-WHITELIST slots carry addr|flags, unused slots take RING_NOPID");

		/* ---- P6A-MOCS: gen12_mocs_table, not tgl_mocs_table ---- */
		parity_get_mocs_settings(&wg.mocs, 12);
		KCHECK(wg.mocs.valid == 1 && wg.mocs.n_entries == 64u &&
			wg.mocs.uc_index == 3u && wg.mocs.unused_entries_index == 2u &&
			wg.mocs.control[3] == (1u | (1u << 2)) &&        /* LE_1_UC|LE_TC_1_LLC */
			wg.mocs.l3cc[3] == (1u << 4) &&                  /* L3_1_UC */
			wg.mocs.control[0] == wg.mocs.control[2] &&      /* gap -> unused idx */
			wg.mocs.control[1] == wg.mocs.control[2] &&
			wg.mocs.control[47] == wg.mocs.control[2] &&
			wg.mocs.control[48] == (3u | (1u << 2) | (3u << 4)),
			"p6a: P6A-MOCS gen12 table, uc_index 3, gaps take the unused entry");
		f.wt_n = 0u;
		{
			unsigned gwr = 0u, lwr = 0u;

			parity_intel_mocs_init(&wg.mocs, &m, &gwr, &lwr);
			KCHECK(gwr == 64u && lwr == 32u &&
				fake_wt_find(&f, 0x4000u + 3u * 4u,
					(1u | (1u << 2)), 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0xb020u + 1u * 4u, 0u, 0u) >= 0,
				"p6a: P6A-MOCS 64 global entries + 32 packed L3CC pairs");
		}

		/* ---- P6B-PAT ---- */
		f.wt_n = 0u;
		{
			unsigned pw = 0u;

			parity_tgl_setup_private_ppat(&m, &pw);
			KCHECK(pw == 8u &&
				fake_wt_find(&f, 0x4800u, 3u, 0xffffffffu) == 0 &&   /* WB */
				fake_wt_find(&f, 0x4804u, 1u, 0xffffffffu) > 0 &&    /* WC */
				fake_wt_find(&f, 0x4808u, 2u, 0xffffffffu) > 0 &&    /* WT */
				fake_wt_find(&f, 0x480cu, 0u, 0xffffffffu) > 0 &&    /* UC */
				fake_wt_find(&f, 0x481cu, 3u, 0xffffffffu) > 0,      /* WB */
				"p6b: P6B-PAT tgl private PPAT = WB,WC,WT,UC,WB,WB,WB,WB");
		}

		/* ---- P6B-APPLY: masked vs plain vs no-verify ---- */
		{
			struct parity_wa_list wl;
			struct parity_wa_apply_result r;

			/*
			 * The MOCS test above consumed all 96 slots of the fake's
			 * generic register store (64 global + 32 L3CC), after which
			 * writes are dropped and reads return 0.  Start clean.
			 */
			fake_mmio_open(&m, &f);

			wl.count = 0u; wl.overflow = 0u; wl.name = "t";
			parity_wa_masked_en(&wl, 0x1000u, 0x4u, 0, "masked");
			parity_wa_write_or(&wl, 0x1004u, 0x8u, 0, "plain");
			parity_wa_add_no_verify(&wl, 0x1008u, 0xffffffffu, 0x55u, 0, "nv");
			osdep_mmio_raw_write32(&m, 0x1000u, 0u);
			osdep_mmio_raw_write32(&m, 0x1004u, 0u);
			osdep_mmio_raw_write32(&m, 0x1008u, 0u);
			f.wt_n = 0u;
			parity_wa_list_apply(&wl, &m, 1, &r);
			KCHECK(r.written == 3u && r.not_verifiable == 1u &&
				r.verified == 2u && r.mismatched == 0u &&
				fake_wt_find(&f, 0x1000u, 0x00040004u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x1004u, 0x8u, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0x1008u, 0x55u, 0xffffffffu) >= 0,
				"p6b: P6B-APPLY masked writes the mask word; no-verify is written but not checked");

			/* A plain WA whose bits are already set is not rewritten. */
			f.wt_n = 0u;
			parity_wa_list_apply(&wl, &m, 0, &r);
			/*
			 * Only the MASKED entry is rewritten: a plain entry whose value
			 * is already correct is skipped, and so is the no-verify one --
			 * "no verify" changes only the readback check, not the apply,
			 * which is still the reference's `val != old || !wa->clr` rule.
			 */
			KCHECK(r.skipped_unchanged == 2u && r.written == 1u,
				"p6b: P6B-APPLY only the masked entry is rewritten when nothing changed");

			/* A register that refuses the write is reported as a mismatch. */
			wl.count = 0u;
			parity_wa_write_or(&wl, 0x100cu, 0x2u, 0, "locked");
			osdep_mmio_raw_write32(&m, 0x100cu, 0u);
			f.gen_n = 0u;   /* make 0x100c read back 0 regardless of writes */
			parity_wa_list_apply(&wl, &m, 1, &r);
			KCHECK(r.mismatched + r.verified == 1u,
				"p6b: P6B-APPLY the verify pass reports, and does not hide, a stuck register");
		}

		/* ---- P6B-RC6: the diagnostic switch ---- */
		{
			struct parity_rc6 rc6;

			for (i = 0u; i < sizeof(rc6); i++) ((char *)&rc6)[i] = 0;
			parity_intel_rc6_init(&rc6, &m);
			f.wt_n = 0u;
			parity_gen11_rc6_enable(&rc6, &m, &wm_gt);
			KCHECK(rc6.enabled == 1 && rc6.wa_disabled == 0 &&
				rc6.ctl_enable == (1u << 18) &&
				/* render|media|sampler PG + HCP/MFX for VCS0 and VCS2 */
				rc6.pg_enable == (0x7u | (1u << 3) | (1u << 4) |
						  (1u << 7) | (1u << 8)) &&
				fake_wt_find(&f, 0xa210u, rc6.pg_enable, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0xa090u, (1u << 18), 0xffffffffu) >= 0,
				"p6b: P6B-RC6 render PG + per-VCS HCP/MFX, then RC_CONTROL");

			for (i = 0u; i < sizeof(rc6); i++) ((char *)&rc6)[i] = 0;
			parity_intel_rc6_init(&rc6, &m);
			parity_gt_test_skip_rc6 = 1;
			f.wt_n = 0u;
			parity_gen11_rc6_enable(&rc6, &m, &wm_gt);
			parity_gt_test_skip_rc6 = 0;
			KCHECK(rc6.wa_disabled == 1 && rc6.enabled == 0 &&
				rc6.pg_enable != 0u &&      /* still computed, for the record */
				fake_wt_find(&f, 0xa210u, rc6.pg_enable, 0xffffffffu) < 0 &&
				fake_wt_find(&f, 0xa0c8u, 60u, 0xffffffffu) >= 0,
				"p6b: P6B-RC6 the diagnostic switch skips PG_ENABLE/RC_CONTROL only");
		}

		/* ---- P6B-RPS: the freq caps decode ---- */
		{
			struct parity_rps rps;

			/* RP_STATE_CAP: rp0 in [7:0], min in [23:16]; 50MHz units. */
			osdep_mmio_raw_write32(&m, 0x140000u + 0x5998u,
				(0x0au << 0) | (0x02u << 16));
			osdep_mmio_raw_write32(&m, 0x140000u + 0x5ef0u, (0x06u << 8));
			for (i = 0u; i < sizeof(rps); i++) ((char *)&rps)[i] = 0;
			parity_intel_rps_init(&rps, &wsb, &m);
			KCHECK(rps.rp0_freq == 30u && rps.min_freq == 6u &&
				rps.rp1_freq == 18u && rps.max_freq == 30u,
				"p6b: P6B-RPS caps are scaled from 50MHz to 16.67MHz units (x3)");
			f.wt_n = 0u;
			parity_intel_rps_enable(&rps, &m);
			KCHECK(rps.enabled == 1 &&
				fake_wt_find(&f, 0xa070u, 0xau, 0xffffffffu) >= 0 &&
				fake_wt_find(&f, 0xa008u, 6u << 23, 0xffffffffu) >= 0,
				"p6b: P6B-RPS enable programs RP_IDLE_HYSTERSIS then RPNSWREQ=min");
		}
	}



	/* ====== P6-c1 prerequisite: the Gen12 forcewake domain map ============== */
	{
		static struct osdep_mmio fwm;
		static struct fake_mmio_state fwf;
		unsigned i;

		/*
		 * The tests elsewhere open the fake with NO range table (everything
		 * always-on).  Here the REAL table is installed, because the point of
		 * the check is the classification itself: a media engine register
		 * reached while only RENDER+GT are held reads as "everything off" and
		 * its writes are dropped, silently.
		 */
		for (i = 0u; i < sizeof(fwf); i++)
			((char *)&fwf)[i] = 0;
		osdep_mmio_init(&fwm, &fake_mmio_backend, &fwf,
			parity_mmio_ranges, parity_mmio_range_count, NULL);

		KCHECK(osdep_mmio_domain_of(&fwm, 0x2000u) == OSDEP_FW_RENDER &&
			osdep_mmio_domain_of(&fwm, 0x229cu) == OSDEP_FW_RENDER &&
			osdep_mmio_domain_of(&fwm, 0x2700u) == OSDEP_FW_GT &&
			osdep_mmio_domain_of(&fwm, 0x2b00u) == OSDEP_FW_GT &&
			osdep_mmio_domain_of(&fwm, 0x2800u) == OSDEP_FW_RENDER,
			"p6c1: P6C1-FWMAP the RCS window is split RENDER/GT as __gen12_fw_ranges has it");

		KCHECK(osdep_mmio_domain_of(&fwm, 0x22000u) == OSDEP_FW_GT &&
			osdep_mmio_domain_of(&fwm, 0x1c0000u) == OSDEP_FW_MEDIA_VDBOX0 &&
			osdep_mmio_domain_of(&fwm, 0x1c3f10u) == OSDEP_FW_MEDIA_VDBOX0 &&
			osdep_mmio_domain_of(&fwm, 0x1d0000u) == OSDEP_FW_MEDIA_VDBOX2 &&
			osdep_mmio_domain_of(&fwm, 0x1d3f10u) == OSDEP_FW_MEDIA_VDBOX2 &&
			osdep_mmio_domain_of(&fwm, 0x1c8000u) == OSDEP_FW_MEDIA_VEBOX0,
			"p6c1: P6C1-FWMAP each media engine maps to its own forcewake domain");

		/* 0x1c4000..0x1c7fff is GEN_FW_RANGE(..., 0): always-on, not VDBOX1. */
		KCHECK(osdep_mmio_domain_of(&fwm, 0x1c4000u) == -1 &&
			osdep_mmio_domain_of(&fwm, 0x1008u) == -1,
			"p6c1: P6C1-FWMAP a reserved or always-on range is not claimed by a domain");

		/*
		 * P6C4-FWGEN: the table is now generated from __gen12_fw_ranges.
		 * These are the ranges the hand-made table got wrong or lacked.
		 */
		KCHECK(osdep_mmio_domain_of(&fwm, 0x8000u) == OSDEP_FW_GT &&      /* MSG_IDLE_CS */
			osdep_mmio_domain_of(&fwm, 0x9550u) == OSDEP_FW_RENDER &&     /* 0x94d0..0x955f */
			osdep_mmio_domain_of(&fwm, 0x9560u) == -1 &&                  /* always-on */
			osdep_mmio_domain_of(&fwm, 0xa2a0u) == OSDEP_FW_GT &&         /* PWRGT_DOMAIN_STATUS */
			osdep_mmio_domain_of(&fwm, 0x3000u) == OSDEP_FW_RENDER &&
			osdep_mmio_domain_of(&fwm, 0x4208u) == OSDEP_FW_GT &&
			osdep_mmio_domain_of(&fwm, 0x40000u) == -1 &&                 /* display */
			osdep_mmio_domain_of(&fwm, 0x138124u) == -1,                  /* PCODE mailbox */
			"p6c4: P6C4-FWGEN the generated table splits 0x9xxx and leaves display/PCODE always-on");
	}

	/* ============ P6-c0: GT objects, the GGTT window and the kernel ppgtt ==== */
	{
		static uint64_t ggtt_table[512];       /* stands for the mapped PTE window */
		static struct parity_gt_mem gm;
		static struct parity_gt_ppgtt pp;
		static const struct drv_dma_constraints c0_cons = {
			39u,           /* address_bits */
			0xffffffffu,   /* max_segment_size */
			0u,            /* segment_boundary */
			1              /* coherent */
		};
		struct drv_dma_device *c0_dma = 0;
		struct parity_gt_object *o1 = 0, *o2 = 0, *sc = 0;
		static struct parity_gt_engine ge;
		static struct parity_engine einfo;
		static struct osdep_mmio em;
		static struct fake_mmio_state ef;
		static struct parity_sseu esseu;
		static struct parity_gt_context lce;
		static struct parity_gt_engine vge;
		static struct parity_engine vinfo;
		static struct parity_gt_context vce;
		static struct parity_gt_request rq;
		static struct parity_wa_list cwal;
		static struct parity_gt_engine bge;
		static struct parity_engine binfo;
		static struct parity_gt_context bce;
		static struct parity_execlists bel;
		static struct parity_rc6 src6;
		static struct parity_rps srps;
		static struct parity_gt_mmio dg;
		static struct parity_gt_init dgi;
		static struct parity_gt_engines des;
		static struct parity_gt_defaults dd;
		uint64_t c0_mask = (((uint64_t)1) << 39) - 1u;
		uint64_t dma0 = 0;
		unsigned i;
		int rc;

		/*
		 * ---- P6C0-ENCODE ----
		 * TGL_CACHELEVEL puts I915_CACHE_NONE at PAT index 3, so the scratch
		 * PTE carries PAT0|PAT1; a PDE is always PPAT_UNCACHED (the same two
		 * bits, by a different name).  Index 4 must reach PAT2 (bit 7), not
		 * wrap into PAT0.
		 */
		KCHECK(parity_gen12_ppgtt_pte_encode(0x1000u, 3u) == (0x1000u | 0x1bu) &&
			parity_gen12_ppgtt_pte_encode(0x1000u, 0u) == (0x1000u | 0x3u) &&
			parity_gen12_ppgtt_pte_encode(0x1000u, 4u) == (0x1000u | 0x83u) &&
			parity_gen8_pde_encode(0x2000u) == (0x2000u | 0x1bu),
			"p6c0: P6C0-ENCODE PAT index 3 sets PAT0|PAT1; a PDE is uncached");

		rc = drv_dma_device_create(&c0_cons, &c0_dma);
		if (rc != 0) {
			KCHECK(0, "p6c0: could not create a DMA device for the GT object tests");
		} else {
			/* ---- P6C0-WINDOW: the window is the TOP of the GGTT (PIN_HIGH) ---- */
			KCHECK(parity_gt_mem_init(&gm, c0_dma, c0_mask, ggtt_table,
					PARITY_GT_GGTT_PAGES, 0xdeadu, 0) == -ENOSPC,
				"p6c0: P6C0-WINDOW a GGTT with no room beyond the window is refused");
			rc = parity_gt_mem_init(&gm, c0_dma, c0_mask, ggtt_table, 512u, 0xdeadu, 0);
			KCHECK(rc == 0 && gm.window_first == 512u - PARITY_GT_GGTT_PAGES &&
				gm.window_pages == PARITY_GT_GGTT_PAGES,
				"p6c0: P6C0-WINDOW the driver window is taken at the top of the GGTT");

			/* ---- P6C0-OBJ: whole pages, zeroed ---- */
			o1 = parity_gt_object_create(&gm, 1u);
			o2 = parity_gt_object_create(&gm, 3u * PARITY_GT_PAGE_BYTES);
			KCHECK(o1 != 0 && o1->pages == 1u && o1->bytes == PARITY_GT_PAGE_BYTES &&
				o2 != 0 && o2->pages == 3u && o2->cpu != 0 &&
				((const unsigned char *)o1->cpu)[0] == 0u &&
				((const unsigned char *)o2->cpu)[3u * PARITY_GT_PAGE_BYTES - 1u] == 0u,
				"p6c0: P6C0-OBJ an object is rounded to whole pages and starts zeroed");

			/* ---- P6C0-BIND: one PTE per page, LM clear, no flush register ---- */
			for (i = 0u; i < 512u; i++)
				ggtt_table[i] = 0u;
			gm.pte_writes = 0u;
			gm.flushes = 0u;
			rc = parity_gt_ggtt_bind(&gm, o2);
			(void)parity_gt_object_page_dma(o2, 0u, &dma0);
			KCHECK(rc == 0 && o2->bound == 1 && gm.pte_writes == 3u && gm.flushes == 1u &&
				o2->ggtt_page == gm.window_first &&
				o2->ggtt_offset == (uint64_t)gm.window_first * PARITY_GT_PAGE_BYTES &&
				ggtt_table[gm.window_first] == (dma0 | 1u),
				"p6c0: P6C0-BIND writes one GGTT PTE per page with the LM bit clear");

			rc = parity_gt_ggtt_bind(&gm, o1);
			KCHECK(rc == 0 && o1->ggtt_page == gm.window_first + 3u &&
				gm.allocated_pages == 4u,
				"p6c0: P6C0-BIND first fit hands out the next free run");

			gm.pte_writes = 0u;
			parity_gt_ggtt_unbind(&gm, o2);
			KCHECK(o2->bound == 0 && gm.pte_writes == 3u && gm.allocated_pages == 1u &&
				ggtt_table[gm.window_first] == 0xdeadu,
				"p6c0: P6C0-BIND unbind points the run back at scratch and frees it");

			/* ---- P6C0-PPGTT: gen8_init_scratch + gen8_alloc_top_pd ---- */
			rc = parity_gt_ppgtt_create(&gm, &pp);
			if (rc == 0) {
				const uint64_t *pt = (const uint64_t *)pp.scratch[1]->cpu;
				const uint64_t *pd = (const uint64_t *)pp.scratch[2]->cpu;
				const uint64_t *pdp = (const uint64_t *)pp.scratch[3]->cpu;
				const uint64_t *pml4 = (const uint64_t *)pp.top_pd->cpu;

				KCHECK(pp.inited == 1 && pp.top == 3 && pp.top_count == 512u &&
					pt[0] == pp.scratch_encode[0] && pt[511] == pp.scratch_encode[0] &&
					pd[0] == pp.scratch_encode[1] && pd[511] == pp.scratch_encode[1] &&
					pdp[0] == pp.scratch_encode[2] &&
					pml4[0] == pp.scratch_encode[3] && pml4[511] == pp.scratch_encode[3],
					"p6c0: P6C0-PPGTT every level is a full page of the level below's encode");
				KCHECK(pp.scratch[0]->bound == 0 && pp.scratch[3]->bound == 0 &&
					pp.top_pd->bound == 0 && pp.top_pd_dma != 0,
					"p6c0: P6C0-PPGTT table pages are reached by DMA address, not bound in the GGTT");
			} else {
				KCHECK(0, "p6c0: P6C0-PPGTT the kernel ppgtt could not be created");
			}

			/* ---- P6C0-SCRATCH: intel_gt_init_scratch(SZ_4K) + PIN_HIGH ---- */
			rc = parity_gt_init_scratch(&gm, &sc);
			KCHECK(rc == 0 && sc != 0 && sc->bytes == PARITY_GT_PAGE_BYTES &&
				sc->pages == 1u && sc->bound == 1,
				"p6c0: P6C0-SCRATCH the gt scratch page is 4 KiB and pinned in the GGTT");


			/* ======== P6-c1: engine setup for execlists submission ======== */
			for (i = 0u; i < sizeof(einfo); i++)
				((char *)&einfo)[i] = 0;
			einfo.id = PARITY_RCS0;
			einfo.class = PARITY_RENDER_CLASS;
			einfo.instance = 0;
			einfo.mmio_base = 0x2000u;
			einfo.name = "rcs0";
			einfo.context_size = 14u * 4096u;   /* GEN11_LR_CONTEXT_RENDER_SIZE */

			esseu.slice_mask = 0x1u;
			esseu.has_slice_pg = 1;
			rc = parity_engine_setup_common(&ge, &einfo, &gm, &esseu);
			parity_execlists_submission_setup(&ge);
			KCHECK(rc == 0 && ge.status_page != 0 && ge.status_page->bound == 1 &&
				ge.status_page->bytes == PARITY_GT_PAGE_BYTES &&
				ge.hwsp_ggtt == ge.status_page->ggtt_offset &&
				ge.submit_reg == 0x2510u && ge.ctrl_reg == 0x2550u &&
				ge.csb_size == 12u && ge.port_mask == 1u &&
				ge.csb_status == (volatile uint64_t *)&ge.hwsp[0x10u] &&
				ge.csb_write == &ge.hwsp[0x2fu],
				"p6c1: P6C1-ENGINE gen11+ puts the CSB at HWSP dword 0x10 and its write pointer at 0x2f");

			/*
			 * The class and instance travel in the UPPER dword of the context
			 * descriptor, so ccid holds them already shifted down by 32.
			 */
			{
				static struct parity_gt_engine ge2;
				static struct parity_engine einfo2;

				for (i = 0u; i < sizeof(einfo2); i++)
					((char *)&einfo2)[i] = 0;
				einfo2.id = PARITY_VCS2;
				einfo2.class = PARITY_VIDEO_DECODE_CLASS;
				einfo2.instance = 2;
				einfo2.mmio_base = 0x1d0000u;
				einfo2.name = "vcs2";
				if (parity_engine_setup_common(&ge2, &einfo2, &gm, &esseu) == 0) {
					parity_execlists_submission_setup(&ge2);
					KCHECK(ge2.ccid == ((2u << (48 - 32)) | (1u << (61 - 32))) &&
						ge2.submit_reg == 0x1d0510u && ge2.ctrl_reg == 0x1d0550u,
						"p6c1: P6C1-ENGINE ccid carries class/instance pre-shifted by 32");
					parity_engine_release(&ge2, &gm);
				} else {
					KCHECK(0, "p6c1: P6C1-ENGINE the vcs2 engine could not be set up");
				}
			}

			/* ---- P6C1-ENABLE: the enable_execlists register sequence ---- */
			fake_mmio_open(&em, &ef);
			ef.wt_n = 0u;
			parity_execlists_enable(&ge, &em);
			KCHECK(fake_wt_find(&ef, 0x2098u, 0xffffffffu, 0xffffffffu) >= 0 &&
				fake_wt_find(&ef, 0x229cu, (8u << 16) | 8u, 0xffffffffu) >= 0 &&
				fake_wt_find(&ef, 0x209cu, 0x100u << 16, 0xffffffffu) >= 0 &&
				fake_wt_find(&ef, 0x2080u, (uint32_t)ge.hwsp_ggtt, 0xffffffffu) >= 0 &&
				ge.resumed == 1,
				"p6c1: P6C1-ENABLE gen11 disables legacy mode, clears STOP_RING and programs HWS_PGA");
			KCHECK(fake_wt_find(&ef, 0x20b0u, 0xffffffffu, 0xffffffffu) >= 0 &&
				fake_wt_find(&ef, 0x20b4u, ~PARITY_I915_ERROR_INSTRUCTION, 0xffffffffu) >= 0,
				"p6c1: P6C1-ENABLE the error interrupt unmasks only I915_ERROR_INSTRUCTION");

			/* ---- P6C1-CSB: reset_csb_pointers ---- */
			ef.wt_n = 0u;
			parity_execlists_reset_csb_pointers(&ge, &em);
			KCHECK(ge.csb_head == 11u && *ge.csb_write == 11u &&
				ge.csb_status[0] == ~(uint64_t)0 &&
				ge.csb_status[11] == ~(uint64_t)0 &&
				ge.csb_reset_writes == 2u &&
				fake_wt_find(&ef, 0x23a0u, (0xffffu << 16) | (11u << 8) | 11u,
					0xffffffffu) >= 0,
				"p6c1: P6C1-CSB the head parks one entry behind and every entry is poisoned");


			/* ============ P6-c2a: the context image and its ring ========== */

			/* ---- P6C2-SIZE: gen12 adds TWO pages after the context ---- */
			{
				unsigned wa_rcs = 0u, wa_xcs = 0u;

				KCHECK(parity_lrc_state_size(14u * 4096u, &wa_rcs) == 16u * 4096u &&
					wa_rcs == 14u &&
					parity_lrc_state_size(2u * 4096u, &wa_xcs) == 4u * 4096u &&
					wa_xcs == 2u,
					"p6c2: P6C2-SIZE INDIRECT_CTX and PER_CTX_BB add two pages, wa_bb_page names the first");
			}

			/* ---- P6C2-RPCS: gen12 requests slices only ---- */
			KCHECK(parity_sseu_make_rpcs(0x1u, 1) ==
					(0x80000000u | (1u << 18) | (1u << 12)) &&
				parity_sseu_make_rpcs(0x1u, 0) == 0u,
				"p6c2: P6C2-RPCS gen12 emits ENABLE|S_CNT_ENABLE|slices and no subslice/EU fields");

			/* ---- P6C2-OFFSETS: the decoded LRI header and register offsets ---- */
			{
				static uint32_t probe_regs[1024];
				unsigned n;

				for (i = 0u; i < 1024u; i++)
					probe_regs[i] = 0u;
				n = parity_lrc_set_offsets(probe_regs, parity_gen12_rcs_offsets_ref(),
					0x2000u, 1);
				KCHECK(probe_regs[0] == 0u &&
					probe_regs[1] == (0x11000019u | (1u << 12) | (1u << 19)) &&
					probe_regs[2] == 0x2244u &&
					probe_regs[4] == 0x2034u &&
					probe_regs[n] == (uint32_t)(0x05000000u | 1u) &&
					n > 0x70u,
					"p6c2: P6C2-OFFSETS NOP skips, LRI is posted+CS_MMIO, REG16 decodes high-group-first");
			}

			/* ---- P6C2-STATE: the image a never-run context starts from ---- */
			rc = parity_lrc_alloc(&lce, &ge, &pp, &gm, 4096u, 1u);
			if (rc == 0) {
				uint32_t *rs;
				uint32_t desc;

				parity_lrc_init_state(&lce);
				rs = lce.lrc_reg_state;
				KCHECK(lce.state_bytes == 16u * 4096u && lce.wa_bb_page == 14u &&
					lce.state->bound == 1 && lce.ring.obj->bound == 1 &&
					lce.lrc_reg_state ==
						(uint32_t *)((char *)lce.state->cpu + 4096u),
					"p6c2: P6C2-STATE the image is ppHWSP then the register state one page in");

				KCHECK(rs[PARITY_CTX_CONTEXT_CONTROL] ==
						(((8u << 16) | 8u) | (1u << 16) | 1u) &&
					rs[PARITY_CTX_PDP0_LDW] == (uint32_t)pp.top_pd_dma &&
					rs[PARITY_CTX_PDP0_UDW] == (uint32_t)(pp.top_pd_dma >> 32),
					"p6c2: P6C2-STATE restore is inhibited and PDP0 names the 4-level top directory");

				KCHECK((rs[PARITY_LRC_MI_MODE_INDEX + 1] & 0x100u) == 0u &&
					(rs[PARITY_LRC_MI_MODE_INDEX + 1] & (0x100u << 16)) != 0u &&
					rs[PARITY_LRC_BB_OFFSET_INDEX + 1] == 0u,
					"p6c2: P6C2-STATE __reset_stop_ring clears STOP_RING and masks it in");

				/* ---- P6C2-RING: the ring registers and the descriptor ---- */
				desc = parity_lrc_update_regs(&lce, 0u);
				KCHECK(rs[PARITY_CTX_RING_START] ==
						(uint32_t)lce.ring.ggtt_offset &&
					rs[PARITY_CTX_RING_HEAD] == 0u &&
					rs[PARITY_CTX_RING_TAIL] == 0u &&
					rs[PARITY_CTX_RING_CTL] == 1u &&
					rs[PARITY_CTX_R_PWR_CLK_STATE] ==
						(0x80000000u | (1u << 18) | (1u << 12)),
					"p6c2: P6C2-RING a 4 KiB ring programs RING_CTL size 0 and render asks for its slice");

				KCHECK(desc == (((uint32_t)lce.state->ggtt_offset | 0x119u) | 4u) &&
					lce.lrca == ((uint32_t)lce.state->ggtt_offset | 0x119u),
					"p6c2: P6C2-RING the descriptor is 64b|VALID|PRIVILEGE and the update forces a restore");


				/* ======== P6-c2b: INDIRECT_CTX and PER_CTX_BB ========= */

				/* ---- P6C2B-AUXINV: gen12_get_aux_inv_reg() ---- */
				KCHECK(parity_lrc_aux_inv_reg(PARITY_RCS0) == 0x4208u &&
					parity_lrc_aux_inv_reg(PARITY_BCS0) == 0x4248u &&
					parity_lrc_aux_inv_reg(PARITY_VCS0) == 0x4218u &&
					parity_lrc_aux_inv_reg(PARITY_VCS2) == 0x4298u &&
					parity_lrc_aux_inv_reg(PARITY_VECS0) == 0x4238u,
					"p6c2b: P6C2B-AUXINV every ADL-P engine has its own AUX_INV register");

				{
					const uint32_t *bb = (const uint32_t *)
						((const char *)lce.state->cpu + lce.wa_bb_page * 4096u);
					uint32_t sgg = (uint32_t)lce.state->ggtt_offset;
					unsigned k;
					int saw_cctl = 0, saw_dbg = 0, saw_sem = 0;

					/* ---- P6C2B-RCS: the batch the render restore runs ---- */
					KCHECK(bb[0] == (0x14800002u | (1u << 22) | (1u << 19)) &&
						bb[1] == 0x2600u &&
						bb[2] == (sgg + 4096u + 0x23u * 4u) &&
						bb[3] == 0u &&
						bb[4] == (0x15000001u | (1u << 18) | (1u << 19)) &&
						bb[6] == 0x23a8u,
						"p6c2b: P6C2B-RCS the timestamp WA loads GPR0 from the saved state and back twice");

					for (k = 0u; k < lce.indirect_bb_dwords; k++) {
						if (bb[k] == 0x2084u)
							saw_cctl = 1;          /* RING_CMD_BUF_CCTL */
						if (bb[k] == 0x20d8u)
							saw_dbg = 1;           /* GEN12_CS_DEBUG_MODE2 */
						if (bb[k] == (0x0e000003u |
								(1u << 16) | (1u << 15) | (4u << 12)))
							saw_sem = 1;           /* MI_SEMAPHORE_WAIT_TOKEN */
					}
					KCHECK(saw_cctl && saw_dbg && saw_sem &&
						lce.indirect_bb_dwords == 32u,
						"p6c2b: P6C2B-RCS render adds the CMD_BUF_CCTL WA and Wa_18022495364, and polls the AUX invalidate");

					/* ---- P6C2B-SLOTS: what the register state is told ---- */
					KCHECK(rs[PARITY_LRC_RING_INDIRECT_PTR + 1] ==
							(lce.indirect_bb_ggtt | 2u) &&
						rs[PARITY_LRC_RING_INDIRECT_OFFSET + 1] == (0xdu << 6) &&
						lce.indirect_bb_ggtt ==
							(sgg + lce.wa_bb_page * 4096u),
						"p6c2b: P6C2B-SLOTS the indirect pointer carries the size in cachelines and the default offset");

					KCHECK(rs[PARITY_LRC_RING_WA_BB_PER_CTX + 1] ==
							((lce.indirect_bb_ggtt + 4096u) | 0x5u) &&
						lce.per_ctx_bb_set == 1 &&
						((const uint32_t *)((const char *)lce.state->cpu +
							(lce.wa_bb_page + 1u) * 4096u))[0] == 0x05000000u,
						"p6c2b: P6C2B-PERCTX the per-context batch is its own terminator, FORCE|VALID");
				}

				/* ---- P6C2B-XCS: a non-render engine gets neither extra WA ---- */
				for (i = 0u; i < sizeof(vinfo); i++)
					((char *)&vinfo)[i] = 0;
				vinfo.id = PARITY_VCS0;
				vinfo.class = PARITY_VIDEO_DECODE_CLASS;
				vinfo.instance = 0;
				vinfo.mmio_base = 0x1c0000u;
				vinfo.context_size = 2u * 4096u;
				vinfo.name = "vcs0";
				if (parity_engine_setup_common(&vge, &vinfo, &gm, &esseu) == 0 &&
				    parity_lrc_alloc(&vce, &vge, &pp, &gm, 4096u, 2u) == 0) {
					const uint32_t *vbb;
					unsigned k;
					int saw_cctl = 0, saw_dbg = 0, saw_inv = 0;

					parity_execlists_submission_setup(&vge);
					parity_lrc_init_state(&vce);
					(void)parity_lrc_update_regs(&vce, 0u);
					vbb = (const uint32_t *)((const char *)vce.state->cpu +
						vce.wa_bb_page * 4096u);
					for (k = 0u; k < vce.indirect_bb_dwords; k++) {
						if (vbb[k] == 0x1c0084u)
							saw_cctl = 1;
						if (vbb[k] == 0x20d8u)
							saw_dbg = 1;
						if (vbb[k] == 0x4218u)
							saw_inv = 1;   /* GEN12_VD0_AUX_INV */
					}
					KCHECK(!saw_cctl && !saw_dbg && saw_inv &&
						vce.wa_bb_page == 2u &&
						vce.indirect_bb_dwords == 32u,
						"p6c2b: P6C2B-XCS a video engine gets the AUX invalidate but neither render-only WA");
					parity_lrc_release(&vce, &gm);
					parity_engine_release(&vge, &gm);
				} else {
					KCHECK(0, "p6c2b: P6C2B-XCS the vcs0 context could not be built");
				}


				/* ======== P6-c3a: what a request writes into its ring ======== */
				{
					const uint32_t *r = (const uint32_t *)lce.ring.vaddr;
					uint32_t hw = (uint32_t)ge.hwsp_ggtt + 0x100u;
					uint32_t fl_inv_bg1 = 0x103070a1u;   /* flush block, no FLUSH_L3 */

					/* ---- P6C3-CREATE: request_alloc's EMIT_INVALIDATE on render ---- */
					rc = parity_request_create(&rq, &lce, 5u, hw, &ge.hwsp[0x40u]);
					KCHECK(rc == 0 && lce.ring.emit == 22u * 4u &&
						r[0] == 0x7a000204u && r[1] == fl_inv_bg1 && r[2] == 0xd0u &&
						r[6] == 0x02800101u &&
						r[7] == 0x7a000004u && r[8] == 0x20344c1cu && r[9] == 0xd0u &&
						r[13] == 0x11020001u && r[14] == 0x4208u && r[15] == 1u &&
						r[16] == 0x0e01c003u && r[18] == 0x4208u &&
						r[21] == 0x02800100u,
						"p6c3: P6C3-CREATE render runs the flush block too (AUX inv), then invalidate + AUX poll");

					/* ---- P6C3-CTXWA: flush(BARRIER), LRI(5) + pairs + NOOP, flush ---- */
					parity_engine_init_ctx_wa(&cwal, &einfo, 12, 3u);
					rc = parity_emit_ctx_wa(&rq, &cwal, &em);
					KCHECK(rc == 0 && cwal.count == 5u &&
						r[22] == 0x7a000204u &&
						r[23] == (fl_inv_bg1 | (1u << 27)) &&
						r[44] == 0x11000009u &&
						r[45] == cwal.list[0].reg && r[46] == cwal.list[0].set &&
						r[55] == 0u &&
						lce.ring.emit == 78u * 4u,
						"p6c3: P6C3-CTXWA a BARRIER flush adds FLUSH_L3; masked and ~0-clear entries are not read");

					/* ---- P6C3-ADD: the render fini breadcrumb and its tail ---- */
					rc = parity_request_add(&rq);
					KCHECK(rc == 0 && rq.added == 1 &&
						r[78] == 0x7a000204u && r[79] == 0x181430a1u && r[80] == 0u &&
						r[84] == 0x7a000004u && r[85] == 0x01104080u &&
						r[86] == hw && r[88] == 5u &&
						r[90] == 0x01000000u && r[91] == 0x04000001u &&
						r[92] == 0x02800000u && r[93] == 0x0e40c003u &&
						r[95] == ((uint32_t)ge.hwsp_ggtt + 0xc8u),
						"p6c3: P6C3-ADD render flushes L3 with DEPTH_STALL, writes the seqno, then waits on PREEMPT");
					KCHECK(rq.tail == 98u * 4u && rq.wa_tail == 100u * 4u &&
						(rq.tail & 7u) == 0u && (rq.wa_tail & 7u) == 0u &&
						r[98] == 0x02800000u && r[99] == 0u &&
						lce.ring.emit == rq.wa_tail,
						"p6c3: P6C3-ADD tail sits before the wa_tail pair and both are qword aligned");
				}

				/* ---- P6C3-BCS: the copy engine's stream and its ctx WA read ---- */
				for (i = 0u; i < sizeof(binfo); i++)
					((char *)&binfo)[i] = 0;
				binfo.id = PARITY_BCS0;
				binfo.class = PARITY_COPY_ENGINE_CLASS;
				binfo.instance = 0;
				binfo.mmio_base = 0x22000u;
				binfo.context_size = 2u * 4096u;
				binfo.name = "bcs0";
				if (parity_engine_setup_common(&bge, &binfo, &gm, &esseu) == 0 &&
				    parity_lrc_alloc(&bce, &bge, &pp, &gm, 4096u, 3u) == 0) {
					const uint32_t *r = (const uint32_t *)bce.ring.vaddr;
					uint32_t hw = (uint32_t)bge.hwsp_ggtt + 0x100u;
					uint32_t want;

					parity_execlists_submission_setup(&bge);
					parity_lrc_init_state(&bce);
					(void)parity_lrc_update_regs(&bce, 0u);

					rc = parity_request_create(&rq, &bce, 7u, hw, &bge.hwsp[0x40u]);
					KCHECK(rc == 0 && bce.ring.emit == 14u * 4u &&
						r[0] == 0x02800101u && r[1] == 0x13254002u && r[2] == 0xd0u &&
						r[6] == 0x4248u && r[13] == 0x02800100u,
						"p6c3: P6C3-BCS the copy invalidate carries MI_FLUSH_DW_CCS, not INVALIDATE_BSD");

					/* BLIT_CCTL is a plain rmw entry: it IS read from the register. */
					parity_engine_init_ctx_wa(&cwal, &binfo, 12, 3u);
					osdep_mmio_raw_write32(&em, cwal.list[0].reg, 0xffffffffu);
					want = (0xffffffffu & ~cwal.list[0].clr) | cwal.list[0].set;
					rc = parity_emit_ctx_wa(&rq, &cwal, &em);
					KCHECK(rc == 0 && cwal.count == 1u &&
						r[28] == 0x11000001u && r[29] == cwal.list[0].reg &&
						r[30] == want && r[31] == 0u,
						"p6c3: P6C3-BCS the plain BLIT_CCTL entry is read, cleared and set before the LRI");

					rc = parity_request_add(&rq);
					KCHECK(rc == 0 &&
						r[46] == 0x13000002u && r[47] == 0u &&
						r[50] == 0x13004002u && r[51] == (hw | 4u) && r[53] == 7u &&
						rq.tail - 46u * 4u == 16u * 4u && (rq.tail & 7u) == 0u &&
						bce.ring.emit - 46u * 4u == 18u * 4u,
						"p6c3: P6C3-BCS the xcs breadcrumb is 18 dwords: flush, USE_GTT store, tail");


					/* ======== P6-c3b: ELSQ submission and the CSB ======== */

					/* ---- P6C3B-PARSE: promote vs completion ---- */
					KCHECK(parity_gen12_csb_parse(((uint64_t)(0x7ffu << 15) << 32) |
							((5u << 15) | 1u)) == 1 &&
						parity_gen12_csb_parse(((uint64_t)(1u << 15) << 32) |
							(0x7ffu << 15)) == 0,
						"p6c3b: P6C3B-PARSE nothing switched away = promote; ctx away + idle to = completion");

					/* ---- P6C3B-SUBMIT: both ports, highest first, then LOAD ---- */
					bge.hwsp[0x32u] = 1u;   /* as reset.prepare leaves it */
					parity_execlists_reset_csb_pointers(&bge, &em);
					KCHECK(bge.hwsp[0x32u] == 0u,
						"p6c4: P6C4-PAUSE reset_csb_pointers clears PREEMPT first (else every breadcrumb spins)");
					parity_execlists_init(&bel);
					ef.wt_n = 0u;
					{
						uint32_t lo = (uint32_t)bce.state->ggtt_offset | 0x119u | 4u;
						uint32_t hi = (1u << 5) | (3u << 29);
						uint32_t pre_tail = rq.tail;

						rc = parity_execlists_submit(&bge, &bel, &em, &rq);
						KCHECK(rc == 0 &&
							fake_wt_find(&ef, 0x22518u, 0u, 0xffffffffu) >= 0 &&
							fake_wt_find(&ef, 0x2251cu, 0u, 0xffffffffu) >= 0 &&
							fake_wt_find(&ef, 0x22510u, lo, 0xffffffffu) >= 0 &&
							fake_wt_find(&ef, 0x22514u, hi, 0xffffffffu) >= 0 &&
							fake_wt_find(&ef, 0x22550u, 1u, 0xffffffffu) >= 0 &&
							fake_wt_find(&ef, 0x22518u, 0u, 0xffffffffu) <
								fake_wt_find(&ef, 0x22510u, lo, 0xffffffffu) &&
							fake_wt_find(&ef, 0x22514u, hi, 0xffffffffu) <
								fake_wt_find(&ef, 0x22550u, 1u, 0xffffffffu),
							"p6c3b: P6C3B-SUBMIT port 1 (empty) then port 0 lo/hi, FORCE_RESTORE on first submit, then EL_CTRL_LOAD");
						KCHECK(bce.lrc_reg_state[PARITY_CTX_RING_TAIL] == pre_tail &&
							rq.tail == rq.wa_tail &&
							(bce.lrc_desc & 4u) == 0u && bce.tag == 0 &&
							(bel.context_tag & 1u) == 0u && bel.serial == 1u &&
							parity_execlists_submit(&bge, &bel, &em, &rq) == -EBUSY,
							"p6c3b: P6C3B-SUBMIT RING_TAIL=rq->tail, rq->tail moves to wa_tail, tag 0 taken, one in flight");
					}

					/* ---- P6C3B-CSB: promote, then complete ---- */
					bge.csb_status[0] = ((uint64_t)(0x7ffu << 15) << 32) | ((1u << 15) | 1u);
					bge.csb_status[1] = ((uint64_t)(1u << 15) << 32) | (0x7ffu << 15);
					*bge.csb_write = 1u;
					{
						struct parity_gt_request *done =
							parity_execlists_process_csb(&bge, &bel, &em);

						KCHECK(done == &rq && bel.promotes == 1u && bel.completes == 1u &&
							bel.have_active == 0 && bel.csb_errors == 0u &&
							(bel.context_tag & 1u) == 1u && bce.tag == -1 &&
							bge.csb_head == 1u &&
							bge.csb_status[0] == ~(uint64_t)0 &&
							bge.csb_status[1] == ~(uint64_t)0,
							"p6c3b: P6C3B-CSB the pair promotes then completes, returns the tag and poisons both slots");
					}

					/* ---- P6C3B-SEQNO: i915_seqno_passed is a signed compare ---- */
					bge.hwsp[0x40u] = 7u;
					{
						int a = parity_request_completed(&rq);

						bge.hwsp[0x40u] = 6u;
						KCHECK(a == 1 && parity_request_completed(&rq) == 0,
							"p6c3b: P6C3B-SEQNO a request is complete once the HWSP seqno reaches it");
					}

					/* ---- P6C3B-FALLBACK: an entry that never became visible ---- */
					osdep_mmio_raw_write32(&em, 0x22000u + 0x370u + 8u * 2u, 0x7ffu << 15);
					osdep_mmio_raw_write32(&em, 0x22000u + 0x370u + 8u * 2u + 4u, 1u << 15);
					*bge.csb_write = 2u;
					(void)parity_execlists_process_csb(&bge, &bel, &em);
					KCHECK(bel.csb_mmio_fallback == 1u &&
						bel.last_csb_lo == (0x7ffu << 15) &&
						bel.last_csb_hi == (1u << 15) &&
						bel.csb_errors == 1u,
						"p6c3b: P6C3B-FALLBACK an all-ones entry falls back to the MMIO status buffer; a completion with nothing active is an error");

					/* ---- P6C3-REFUSE: the reference's GEM_BUG_ONs become refusals ---- */
					KCHECK(parity_request_create(&rq, &bce, 8u, hw | 0x20u, 0) == -EINVAL &&
						parity_request_create(&rq, &bce, 8u, hw | 4u, 0) == -EINVAL,
						"p6c3: P6C3-REFUSE a breadcrumb address with bit 5 set or not qword aligned is refused");
					rq.ce = &bce;
					rq.error = 0;
					KCHECK(parity_ring_begin(&rq, 3u) == 0 && rq.error == -EINVAL,
						"p6c3: P6C3-REFUSE an odd dword count is refused (RING_TAIL must stay qword aligned)");


					/* ============ P6-c4a: the gt_resume pieces ============ */

					/* ---- P6C4-STOPCS: STOP_RING, prefetch off, MODE_IDLE ---- */
					ef.wt_n = 0u;
					osdep_mmio_raw_write32(&em, 0x22034u, 0u);   /* HEAD */
					osdep_mmio_raw_write32(&em, 0x22030u, 0u);   /* TAIL */
					rc = parity_engine_stop_cs(&bge, &em);
					KCHECK(rc == 0 &&
						fake_wt_find(&ef, 0x2209cu, (0x100u << 16) | 0x100u, 0xffffffffu) >= 0 &&
						fake_wt_find(&ef, 0x2229cu, (0x400u << 16) | 0x400u, 0xffffffffu) >= 0,
						"p6c4: P6C4-STOPCS STOP_RING and (Wa_22011802037) PREFETCH_DISABLE; an empty ring is not a timeout");
					osdep_mmio_raw_write32(&em, 0x22034u, 0x40u);
					KCHECK(parity_engine_stop_cs(&bge, &em) == -ETIMEDOUT,
						"p6c4: P6C4-STOPCS a ring that still holds work IS a timeout");
					osdep_mmio_raw_write32(&em, 0x22034u, 0u);

					/* ---- P6C4-MIFW: Wa_22011802037's pending MI_FORCE_WAKE ---- */
					osdep_mmio_raw_write32(&em, 0x800cu, (1u << 25) | (1u << 9));
					osdep_mmio_raw_write32(&em, 0xa2a0u, 1u);
					parity_engine_wait_for_pending_mi_fw(&bge, &em);
					KCHECK(bge.mi_fw_pending == 1u,
						"p6c4: P6C4-MIFW pending = bits[29:25] & bits[13:9], read from MSG_IDLE_BCS");
					osdep_mmio_raw_write32(&em, 0x800cu, 1u << 9);   /* req without ack */
					parity_engine_wait_for_pending_mi_fw(&bge, &em);
					KCHECK(bge.mi_fw_pending == 0u,
						"p6c4: P6C4-MIFW a request bit without its mirror is not pending");

					/* ---- P6C4-RC6SAN / RPSSAN ---- */
					src6.supported = 1;
					src6.enabled = 1;
					ef.wt_n = 0u;
					parity_intel_rc6_sanitize(&src6, &em);
					KCHECK(src6.enabled == 0 &&
						fake_wt_find(&ef, 0xa210u, 0u, 0xffffffffu) >= 0 &&
						fake_wt_find(&ef, 0xa090u, 0u, 0xffffffffu) >= 0 &&
						fake_wt_find(&ef, 0xa094u, 0u, 0xffffffffu) >= 0,
						"p6c4: P6C4-RC6SAN PG_ENABLE, RC_CONTROL and RC_STATE go to 0 before init_hw");
					src6.supported = 0;
					ef.wt_n = 0u;
					parity_intel_rc6_sanitize(&src6, &em);
					parity_intel_rps_sanitize(&srps, &em);
					KCHECK(fake_wt_find(&ef, 0xa090u, 0u, 0xffffffffu) < 0 &&
						fake_wt_find(&ef, 0xa168u, 0xffffffffu, 0xffffffffu) >= 0,
						"p6c4: P6C4-RPSSAN unsupported rc6 writes nothing; rps masks every PM interrupt");

					/* ---- P6C4-LRCRESET: lrc_reset() on a used context ---- */
					bce.lrc_reg_state[PARITY_CTX_CONTEXT_CONTROL] = 0u;
					parity_lrc_reset(&bce);
					KCHECK(bce.ring.head == bce.ring.emit &&
						bce.ring.tail == bce.ring.emit &&
						bce.lrc_reg_state[PARITY_CTX_CONTEXT_CONTROL] ==
							(((8u << 16) | 8u) | (1u << 16) | 1u) &&
						bce.lrc_reg_state[PARITY_CTX_RING_HEAD] == bce.ring.emit &&
						(bce.lrc_desc & 4u) != 0u,
						"p6c4: P6C4-LRCRESET the ring restarts at emit, the registers are scrubbed, restore is forced");

					parity_lrc_release(&bce, &gm);
					parity_engine_release(&bge, &gm);
				} else {
					KCHECK(0, "p6c3: P6C3-BCS the bcs0 context could not be built");
				}

				parity_lrc_release(&lce, &gm);
				KCHECK(lce.allocated == 0 && lce.state == 0 && lce.ring.obj == 0,
					"p6c2: P6C2-STATE release gives the image and the ring back");
			} else {
				KCHECK(0, "p6c2: P6C2-STATE the context image could not be allocated");
			}

			parity_engine_release(&ge, &gm);


			/* ====== P6-c4b: __engines_record_defaults, HW simulated ====== */
			spin_init(&ktest_wedge_lock, LOCK_RANK_DEVICE, "ktest-wedge");
			for (i = 0u; i < sizeof(dg); i++)
				((char *)&dg)[i] = 0;
			for (i = 0u; i < sizeof(dgi); i++)
				((char *)&dgi)[i] = 0;
			dg.engines[0].id = PARITY_BCS0;
			dg.engines[0].class = PARITY_COPY_ENGINE_CLASS;
			dg.engines[0].instance = 0;
			dg.engines[0].mmio_base = 0x22000u;
			dg.engines[0].context_size = 2u * 4096u;
			dg.engines[0].name = "bcs0";
			dg.num_engines = 1u;
			dg.sseu.slice_mask = 1u;
			dg.sseu.has_slice_pg = 1;
			parity_engine_init_ctx_wa(&dgi.ctx_wa[0], &dg.engines[0], 12, 3u);
			fake_mmio_open(&em, &ef);

			rc = parity_intel_engines_init(&des, &dg, &gm, &pp);
			if (rc == 0) {
				struct parity_gt_engine *e0 = &des.ge[0];
				volatile uint32_t *tl;
				unsigned busy;

				parity_execlists_reset_csb_pointers(e0, &em);
				KCHECK(des.n == 1u && des.kernel_ce[0].allocated == 1 &&
					des.kernel_ce[0].ring.size == 4096u,
					"p6c4: P6C4B-INIT intel_engines_init pins a 4 KiB kernel context per engine");

				/* ---- P6C4B-SUBMIT: the record request goes out ---- */
				ef.wt_n = 0u;
				rc = parity_engines_record_defaults_submit(&dd, &des, &dgi, &gm, &pp, &em);
				tl = (volatile uint32_t *)dd.tl_page[0]->cpu;
				KCHECK(rc == 0 && dd.state[0] == PARITY_DEF_RECORD &&
					dd.rq[0].seqno == 2u && dd.tl_page[0]->bound == 1 &&
					dd.rq[0].hwsp_ggtt == (uint32_t)dd.tl_page[0]->ggtt_offset &&
					dd.ce[0].ring.size == 4096u &&
					fake_wt_find(&ef, 0x22550u, 1u, 0xffffffffu) >= 0,
					"p6c4: P6C4B-SUBMIT a new context on its own timeline page, seqno 2 (initial breadcrumb), submitted");

				busy = parity_engines_record_defaults_poll(&dd, &des, &em);
				KCHECK(busy == 1u && dd.state[0] == PARITY_DEF_RECORD,
					"p6c4: P6C4B-WAIT nothing happens until the breadcrumb lands and the CSB reports");

				/* The engine runs it: seqno, then promote + complete. */
				tl[0] = 2u;
				e0->csb_status[0] = ((uint64_t)(0x7ffu << 15) << 32) | ((1u << 15) | 1u);
				e0->csb_status[1] = ((uint64_t)(1u << 15) << 32) | (0x7ffu << 15);
				*e0->csb_write = 1u;
				ef.wt_n = 0u;
				busy = parity_engines_record_defaults_poll(&dd, &des, &em);
				KCHECK(busy == 1u && dd.state[0] == PARITY_DEF_SWITCH &&
					dd.krq[0].ce == &des.kernel_ce[0] && dd.krq[0].seqno == 1u &&
					dd.krq[0].hwsp_ggtt == (uint32_t)e0->hwsp_ggtt + 0x100u &&
					des.el[0].serial == des.el[0].wakeref_serial &&
					fake_wt_find(&ef, 0x22550u, 1u, 0xffffffffu) >= 0,
					"p6c4: P6C4B-PARK retirement parks the engine: a kernel-context request switches the record context out");

				/* The switch completes. */
				e0->hwsp[0x40u] = 1u;
				e0->csb_status[2] = ((uint64_t)(0x7ffu << 15) << 32) | ((1u << 15) | 1u);
				e0->csb_status[3] = ((uint64_t)(1u << 15) << 32) | (0x7ffu << 15);
				*e0->csb_write = 3u;
				busy = parity_engines_record_defaults_poll(&dd, &des, &em);
				KCHECK(busy == 0u && dd.state[0] == PARITY_DEF_PARKED && dd.err == 0,
					"p6c4: P6C4B-PARK the engine is idle once the kernel-context request retires too");

				/* ---- P6C4B-DEFAULT: the saved image becomes the default state ---- */
				rc = parity_engines_record_defaults_finish(&dd, &des, &gm);
				KCHECK(rc == 0 && dd.default_state[0] != 0 &&
					dd.default_state[0]->bytes == dd.ce[0].state_bytes &&
					dd.default_state[0]->bound == 0 &&
					memcmp(dd.default_state[0]->cpu, dd.ce[0].state->cpu,
						dd.ce[0].state_bytes) == 0,
					"p6c4: P6C4B-DEFAULT the whole switched-out image is copied as engine->default_state");
				/* ---- P6C4B-INHERIT: a later context starts from that image ---- */
				{
					static struct parity_gt_context ice;
					uint32_t want[4];
					uint32_t *img = (uint32_t *)dd.default_state[0]->cpu;

					img[4096u / 4u + 0x40u] = 0x12345678u;   /* a mark in the engine state */
					e0->default_state = dd.default_state[0];
					rc = parity_lrc_alloc(&ice, e0, &pp, &gm, 4096u, 0u);
					want[0] = want[1] = want[2] = want[3] = 0u;
					if (rc == 0) {
						parity_lrc_init_state(&ice);
						(void)parity_lrc_update_regs(&ice, 0u);
						want[0] = ice.lrc_reg_state[0x40u];
						want[1] = ice.lrc_reg_state[PARITY_CTX_CONTEXT_CONTROL];
						want[2] = ((uint32_t *)ice.state->cpu)[0];        /* ppHWSP cleared */
						want[3] = ice.lrc_reg_state[PARITY_CTX_RING_START];
					}
					KCHECK(rc == 0 && want[0] == 0x12345678u && (want[1] & 1u) == 0u &&
						want[2] == 0u && want[3] == (uint32_t)ice.ring.ggtt_offset,
						"p6c4: P6C4B-INHERIT lrc_init_state copies engine->default_state, clears the ppHWSP, and does not inhibit the restore");
					if (rc == 0)
						parity_lrc_release(&ice, &gm);
					e0->default_state = 0;
				}

				parity_engines_defaults_release(&dd, &gm);

				/* ---- P6C4B-WEDGE: nothing lands -> -ETIME -> wedge ---- */
				rc = parity_engines_record_defaults(&dd, &des, &dgi, &gm, &pp, &em,
					&ktest_wedge_lock, 1u);
				KCHECK(rc == -EIO && dd.timed_out == 1 && dd.wedged == 1 &&
					dd.ce[0].allocated == 0 && dd.tl_page[0] == 0 &&
					dd.default_state[0] == 0,
					"p6c4: P6C4B-WEDGE a timeout is -EIO, the GT is wedged (reset) and the contexts are still put");
				parity_engines_defaults_release(&dd, &gm);
				parity_intel_engines_release(&des, &gm);
			} else {
				KCHECK(0, "p6c4: P6C4B-INIT intel_engines_init failed on the simulated engine");
			}


			/* ====== P6-c5: __engines_verify_workarounds, HW simulated ====== */
			{
				static struct parity_gt_verify_wa vw;
				struct parity_wa_list *wl = &dgi.engine_wa[0];
				const uint64_t csb_promote = ((uint64_t)(0x7ffu << 15) << 32) | ((1u << 15) | 1u);
				const uint64_t csb_complete = ((uint64_t)(1u << 15) << 32) | (0x7ffu << 15);

				wl->count = 0u; wl->overflow = 0u; wl->name = "bcs0";
				parity_wa_masked_en(wl, 0x2209cu, 0x8u, 0, "t: masked");            /* [0] */
				parity_wa_write_or(wl, 0x22050u, 0x100u, 0, "t: plain");            /* [1] */
				parity_wa_write_or(wl, 0xb134u, 0x1u, 1, "t: in an MCR range");    /* [2]: not via CS */
				parity_wa_add_no_verify(wl, 0x22060u, 0u, 0x5u, 0, "t: no verify"); /* [3] */
				fake_mmio_open(&em, &ef);
				rc = parity_intel_engines_init(&des, &dg, &gm, &pp);
				KCHECK(rc == 0, "p6c5: P6C5-INIT intel_engines_init for the verify run");
				if (rc == 0) {
					struct parity_gt_engine *e0 = &des.ge[0];
					const uint32_t *ring = des.kernel_ce[0].ring.vaddr;
					volatile uint32_t *results;
					uint32_t sg;
					unsigned k, e;
					int found[4] = { 0, 0, 0, 0 };
					int busy;

					parity_execlists_reset_csb_pointers(e0, &em);
					ef.wt_n = 0u;
					rc = parity_engine_verify_wa_submit(&vw, 0u, &des, wl, &gm, &em);
					sg = vw.scratch[0] != 0 ? (uint32_t)vw.scratch[0]->ggtt_offset : 0u;
					for (k = 0u; k + 3u < des.kernel_ce[0].ring.size / 4u; k++) {
						if (ring[k] != (PARITY_MI_STORE_REGISTER_MEM_GEN8 |
						    PARITY_MI_SRM_LRM_GLOBAL_GTT) || ring[k + 3u] != 0u)
							continue;
						for (e = 0u; e < 4u; e++)
							if (ring[k + 1u] == wl->list[e].reg &&
							    ring[k + 2u] == sg + 4u * e)
								found[e] = 1;
					}
					KCHECK(rc == 0 && vw.state[0] == PARITY_VWA_SRM &&
						vw.rq[0].ce == &des.kernel_ce[0] && vw.emitted[0] == 3u &&
						vw.mcr_skipped[0] == 1u && vw.scratch[0] != 0 &&
						vw.scratch[0]->bound == 1 &&
						found[0] && found[1] && !found[2] && found[3] &&
						fake_wt_find(&ef, 0x22550u, 1u, 0xffffffffu) >= 0,
						"p6c5: P6C5-SRM one SRM per non-MCR entry to scratch + 4 * list index, on the kernel context, submitted");

					busy = parity_engine_verify_wa_poll(&vw, 0u, &des, &em);
					KCHECK(busy && vw.state[0] == PARITY_VWA_SRM,
						"p6c5: P6C5-WAIT nothing happens until the breadcrumb lands and the CSB reports");

					/* The engine stores the registers and completes. */
					results = (volatile uint32_t *)vw.scratch[0]->cpu;
					results[0] = 0x00000008u;     /* masked: low bits carry the value */
					results[1] = 0x00000100u;
					results[3] = 0x00000000u;     /* no-verify: never compared */
					e0->hwsp[0x40u] = vw.rq[0].seqno;
					e0->csb_status[0] = csb_promote;
					e0->csb_status[1] = csb_complete;
					*e0->csb_write = 1u;
					busy = parity_engine_verify_wa_poll(&vw, 0u, &des, &em);
					rc = parity_wa_list_check(&vw, 0u, wl, "load");
					KCHECK(!busy && vw.state[0] == PARITY_VWA_DONE && rc == 0 &&
						vw.verified[0] == 2u && vw.not_verifiable[0] == 1u &&
						vw.mismatched[0] == 0u,
						"p6c5: P6C5-VERIFY stored values are compared as (cur ^ set) & read; MCR and read-mask-0 entries are not");

					results[1] = 0x00000000u;
					rc = parity_wa_list_check(&vw, 0u, wl, "load");
					KCHECK(rc == -ENXIO && vw.mismatched[0] == 1u && vw.verified[0] == 1u,
						"p6c5: P6C5-LOST a lost workaround is -ENXIO and is counted");

					ef.wt_n = 0u;
					rc = parity_engine_verify_wa_park(&vw, 0u, &des, &em);
					KCHECK(rc == 0 && vw.state[0] == PARITY_VWA_SWITCH &&
						vw.krq[0].seqno == vw.rq[0].seqno + 1u &&
						des.el[0].wakeref_serial == des.el[0].serial &&
						fake_wt_find(&ef, 0x22550u, 1u, 0xffffffffu) >= 0,
						"p6c5: P6C5-PARK pm_put parks the engine: a kernel-context switch request is submitted");

					e0->hwsp[0x40u] = vw.krq[0].seqno;
					e0->csb_status[2] = csb_promote;
					e0->csb_status[3] = csb_complete;
					*e0->csb_write = 3u;
					busy = parity_engine_verify_wa_poll(&vw, 0u, &des, &em);
					KCHECK(!busy && vw.state[0] == PARITY_VWA_PARKED,
						"p6c5: P6C5-IDLE the engine is idle once the switch request retires too");
					parity_engines_verify_wa_release(&vw, &gm);

					/* The whole thing with nothing answering: -ETIME, reported -EIO. */
					rc = parity_engines_verify_workarounds(&vw, &des, &dgi, &gm, &em, 1u);
					KCHECK(rc == -EIO && vw.timed_out == 1 && vw.engine_err[0] == -ETIME &&
						vw.state[0] == PARITY_VWA_SRM && vw.krq[0].seqno == 0u,
						"p6c5: P6C5-TIME an unanswered SRM request is -ETIME -> -EIO; no park switch is queued behind it");
					parity_engines_verify_wa_release(&vw, &gm);

					/* if (!wal->count) return 0: no request, no scratch, no park. */
					wl->count = 0u;
					rc = parity_engines_verify_workarounds(&vw, &des, &dgi, &gm, &em, 1u);
					KCHECK(rc == 0 && vw.state[0] == PARITY_VWA_IDLE && vw.scratch[0] == 0 &&
						vw.polls == 0u,
						"p6c5: P6C5-EMPTY an engine without workarounds submits nothing");
					parity_engines_verify_wa_release(&vw, &gm);
					parity_intel_engines_release(&des, &gm);
				}
			}


			/* ====== P6-c6: intel_migrate_init ====== */
			{
				static struct parity_gt_migrate mg;
				unsigned live0 = gm.objects_live, pages0 = gm.allocated_pages;

				rc = parity_intel_engines_init(&des, &dg, &gm, &pp);
				KCHECK(rc == 0, "p6c6: P6C6-INIT intel_engines_init for the migrate run");
				if (rc == 0) {
					unsigned live1 = gm.objects_live;

					rc = parity_intel_migrate_init(&mg, &des, &gm);
					KCHECK(rc == 0 && mg.inited && mg.has_engine && mg.engine_idx == 0u &&
						mg.err == 0,
						"p6c6: P6C6-ENGINE the first copy engine (bcs0) owns the migrate context");
					if (rc == 0) {
						const uint64_t *pml4 = (const uint64_t *)mg.vm.top_pd->cpu;
						const struct parity_gt_ppgtt_table *t = mg.vm.tables;
						int tree_ok = mg.vm.n_tables == 11u &&
							t[0].lvl == 2 && t[0].parent == mg.vm.top_pd && t[0].idx == 0u &&
							t[1].lvl == 1 && t[1].parent == t[0].obj && t[1].idx == 0u;
						int pts_ok = 1, window_ok = 1;
						unsigned k;

						for (k = 0u; k < 9u && tree_ok; k++)
							if (t[2u + k].lvl != 0 || t[2u + k].parent != t[1].obj ||
							    t[2u + k].idx != k)
								pts_ok = 0;
						if (tree_ok && pts_ok) {
							const uint64_t *pdp = (const uint64_t *)t[0].obj->cpu;
							const uint64_t *pd = (const uint64_t *)t[1].obj->cpu;
							const uint64_t *pt0 = (const uint64_t *)t[2].obj->cpu;
							const uint64_t *win = (const uint64_t *)t[10].obj->cpu;

							tree_ok = pml4[0] == parity_gen8_pde_encode_cached(t[0].dma) &&
								pml4[1] == mg.vm.scratch_encode[3] &&
								pdp[0] == parity_gen8_pde_encode_cached(t[1].dma) &&
								pdp[1] == mg.vm.scratch_encode[2] &&
								pd[9] == mg.vm.scratch_encode[1] &&
								pt0[0] == mg.vm.scratch_encode[0] &&
								pt0[511] == mg.vm.scratch_encode[0];
							for (k = 0u; k < 9u; k++)
								if (pd[k] != parity_gen8_pde_encode_cached(t[2u + k].dma))
									tree_ok = 0;
							/* insert_pte(): PT k of the windows sits at 16M + 4K * k, uncached. */
							for (k = 0u; k < 8u; k++)
								if (win[k] != parity_gen12_ppgtt_pte_encode(t[2u + k].dma,
								    PARITY_PAT_INDEX_CACHE_NONE))
									window_ok = 0;
							if (win[8] != mg.vm.scratch_encode[0])
								window_ok = 0;
						}
						KCHECK(tree_ok && pts_ok,
							"p6c6: P6C6-VM allocate_va_range(0, 16M + 32K): PDP, PD, nine PTs (cached PDEs), scratch elsewhere");
						KCHECK(window_ok && mg.pte_window == 2ull * PARITY_MIGRATE_CHUNK_SZ &&
							mg.exposed_pts == 8u && mg.window_bytes == 16ull << 20,
							"p6c6: P6C6-PTE the PTE window maps the eight window page tables themselves");
						KCHECK(mg.ce.allocated && mg.ce.ring.size == PARITY_MIGRATE_RING_BYTES &&
							mg.ce.ring.obj != 0 && mg.ce.ring.obj->bound && mg.ce.ring.obj->contiguous &&
							mg.ce.state != 0 && mg.ce.state->bound && mg.ce.vm == &mg.vm &&
							mg.ce.lrc_reg_state[PARITY_CTX_PDP0_LDW] == (uint32_t)mg.vm.top_pd_dma &&
							mg.ce.lrc_reg_state[PARITY_CTX_PDP0_UDW] == (uint32_t)(mg.vm.top_pd_dma >> 32) &&
							mg.ce.lrc_reg_state[PARITY_CTX_RING_CTL] == ((PARITY_MIGRATE_RING_BYTES - 4096u) | 1u) &&
							mg.hwsp_ggtt == (uint32_t)des.ge[0].hwsp_ggtt + 0x108u &&
							mg.hwsp_cpu == &des.ge[0].hwsp[0x42u] && mg.tl_seqno == 0u,
							"p6c6: P6C6-CTX a pinned 512 KiB-ring context on the migrate vm, timeline at HWS_MIGRATE");
					}
					parity_intel_migrate_fini(&mg, &gm);
					KCHECK(gm.objects_live == live1 && mg.inited == 0 && mg.vm.n_tables == 0u,
						"p6c6: P6C6-FINI the pinned context and the whole migrate vm are released");
					parity_intel_engines_release(&des, &gm);
					KCHECK(gm.objects_live == live0 && gm.allocated_pages == pages0,
						"p6c6: P6C6-LEAK nothing is left behind");
				}
			}


			/* ====== P7: hotplug irq setup, IPC, DC_off disable (DC6), PXP ====== */
			{
				static struct parity_hotplug hp;
				static struct parity_pxp px;
				struct parity_power_well dcw;
				struct parity_pw_ctx dcc;
				unsigned i7;

				/* ---- P7-HPD-PIN: intel_ddi_init()'s hpd pin per port ---- */
				KCHECK(parity_intel_ddi_hpd_pin(13, 0) == PARITY_HPD_PORT_A &&
					parity_intel_ddi_hpd_pin(13, 1) == PARITY_HPD_PORT_B &&
					parity_intel_ddi_hpd_pin(13, 3) == PARITY_HPD_PORT_TC1 &&
					parity_intel_ddi_hpd_pin(13, 7) == PARITY_HPD_PORT_D &&
					parity_intel_ddi_hpd_pin(12, 3) == PARITY_HPD_PORT_TC1,
					"p7: P7-HPD-PIN DDI A/B default pins, TC1+ from PORT_TC1 (ver 12+), D/E from PORT_D_XELPD (ver 13)");

				/* ---- P7-HPD-SETUP: gen11 + icp registers for encoders on A and B ---- */
				for (i7 = 0u; i7 < sizeof(hp); i7++)
					((char *)&hp)[i7] = 0;
				hp.encoder_pin[0] = PARITY_HPD_PORT_A;
				hp.encoder_pin[1] = PARITY_HPD_PORT_B;
				hp.n_encoders = 2u;
				fake_mmio_open(&em, &ef);
				fake_gen_set(&ef, PARITY_SHOTPLUG_CTL_DDI, 0x8888u);
				fake_gen_set(&ef, PARITY_SHOTPLUG_CTL_TC, 0x00888888u);
				fake_gen_set(&ef, PARITY_GEN11_TC_HOTPLUG_CTL, 0x00888888u);
				fake_gen_set(&ef, PARITY_GEN11_TBT_HOTPLUG_CTL, 0x00888888u);
				fake_gen_set(&ef, PARITY_SDEIMR, 0xffffffffu);
				fake_gen_set(&ef, PARITY_GEN11_DE_HPD_IMR, 0xffffffffu);
				ef.wt_n = 0u;
				parity_intel_hpd_init(&hp, &em, 13, PARITY_PCH_ADP, 1, 1);
				KCHECK(hp.irq_setups == 1u && hp.state[PARITY_HPD_PORT_A] == PARITY_HPD_ENABLED &&
					hp.de_hotplug_irqs == 0u && hp.de_enabled_irqs == 0u &&
					hp.pch_hotplug_irqs == 0x30000u && hp.pch_enabled_irqs == 0x30000u &&
					fake_gen_get(&ef, PARITY_SHOTPLUG_CTL_DDI) == 0x88u &&
					fake_gen_get(&ef, PARITY_SHOTPLUG_CTL_TC) == 0u &&
					fake_gen_get(&ef, PARITY_GEN11_TC_HOTPLUG_CTL) == 0u &&
					fake_gen_get(&ef, PARITY_GEN11_TBT_HOTPLUG_CTL) == 0u &&
					fake_gen_get(&ef, PARITY_SDEIMR) == 0xfffcffffu &&
					fake_wt_find(&ef, PARITY_SHPD_FILTER_CNT, PARITY_SHPD_FILTER_CNT_250, 0xffffffffu) >= 0 &&
					fake_wt_find(&ef, PARITY_GEN11_DE_HPD_IMR, 0u, 0u) < 0 &&
					hp.sdeimr_skipped == 0,
					"p7: P7-HPD-SETUP DDI A/B enabled in SHOTPLUG_CTL_DDI, TC/TBT cleared, SDEIMR unmasks A/B, filter 250, DE HPD IMR untouched");

				/* ---- P7-HPD-NOIRQ: SDEIMR is not written before intel_irq_install ---- */
				fake_gen_set(&ef, PARITY_SDEIMR, 0xffffffffu);
				ef.wt_n = 0u;
				parity_intel_hpd_init(&hp, &em, 13, PARITY_PCH_ADP, 1, 0);
				KCHECK(hp.sdeimr_skipped == 1 && fake_gen_get(&ef, PARITY_SDEIMR) == 0xffffffffu &&
					fake_wt_find(&ef, PARITY_SDEIMR, 0u, 0u) < 0,
					"p7: P7-HPD-NOIRQ ibx_display_interrupt_update() refuses while intel_irqs_enabled() is false");
				parity_intel_hpd_init(&hp, &em, 13, PARITY_PCH_ADP, 0, 1);
				KCHECK(hp.irq_setup_skipped == 1,
					"p7: P7-HPD-NODISP hpd_irq_setup needs display_irqs_enabled");

				/* ---- P7-IPC: DISP_ARB_CTL2.DISP_IPC_ENABLE ---- */
				fake_gen_set(&ef, PARITY_DISP_ARB_CTL2, 0u);
				KCHECK(parity_skl_watermark_ipc_init(&em, 1, 1) == 1 &&
					fake_gen_get(&ef, PARITY_DISP_ARB_CTL2) == PARITY_DISP_IPC_ENABLE &&
					parity_skl_watermark_ipc_init(&em, 0, 1) == 0,
					"p7: P7-IPC skl_watermark_ipc_init sets DISP_IPC_ENABLE only with HAS_IPC");

				/* ---- P7-DC6: the DC_off well's disable enables the target DC state ---- */
				for (i7 = 0u; i7 < sizeof(dcw); i7++) ((char *)&dcw)[i7] = 0;
				for (i7 = 0u; i7 < sizeof(dcc); i7++) ((char *)&dcc)[i7] = 0;
				dcw.name = "DC_off"; dcw.ops = PARITY_PW_OPS_DC_OFF;
				dcc.mmio = &em; dcc.display_ver = 13;
				dcc.allowed_dc_mask = 0x4000000au; dcc.target_dc_state = PARITY_DC_STATE_EN_UPTO_DC6;
				fake_gen_set(&ef, 0x45504u, 0u);
				dcc.dmc_has_payload = 0;
				(void)parity_power_well_disable(&dcw, &dcc);
				KCHECK(fake_gen_get(&ef, 0x45504u) == 0u && dcc.dc_state_writes == 0u &&
					dcw.hw_enabled == 1,
					"p7: P7-DC6-NODMC without a DMC payload no DC state is enabled and the well stays on");
				dcc.dmc_has_payload = 1;
				(void)parity_power_well_disable(&dcw, &dcc);
				KCHECK(fake_gen_get(&ef, 0x45504u) == PARITY_DC_STATE_EN_UPTO_DC6 &&
					dcc.dc_state == PARITY_DC_STATE_EN_UPTO_DC6 && dcc.dc_state_writes == 1u &&
					dcw.hw_enabled == 0 && parity_power_well_is_enabled(&dcw, &dcc) == 0,
					"p7: P7-DC6 skl_enable_dc6: DC_STATE_EN gets UPTO_DC6, the well reads as off");
				dcw.hw_enabled = 0;
				(void)parity_power_well_enable(&dcw, &dcc);
				KCHECK((fake_gen_get(&ef, 0x45504u) & 0x40000003u) == 0u &&
					parity_power_well_is_enabled(&dcw, &dcc) == 1,
					"p7: P7-DC6-OFF enabling the well again clears the DC states");

				/* ---- P7-PXP: pinned VCS context on the GT vm ---- */
				dg.engines[1].id = PARITY_VCS0;
				dg.engines[1].class = PARITY_VIDEO_DECODE_CLASS;
				dg.engines[1].instance = 0;
				dg.engines[1].mmio_base = 0x1c0000u;
				dg.engines[1].context_size = 2u * 4096u;
				dg.engines[1].name = "vcs0";
				dg.num_engines = 2u;
				fake_mmio_open(&em, &ef);
				rc = parity_intel_engines_init(&des, &dg, &gm, &pp);
				if (rc == 0) {
					unsigned live1 = gm.objects_live;

					rc = parity_intel_pxp_init(&px, &des, &pp, &gm, 0);
					KCHECK(rc == -ENODEV && !px.inited,
						"p7: P7-PXP-NONE without has_pxp there is no PXP GT");
					rc = parity_intel_pxp_init(&px, &des, &pp, &gm, 1);
					KCHECK(rc == 0 && px.inited && px.full_feature && px.engine_idx == 1u &&
						px.kcr_base == 0x32000u && px.ce.allocated && px.ce.ring.size == 4096u &&
						px.ce.vm == &pp && px.stream_cmd != 0 && px.component_added &&
						px.hwsp_ggtt == (uint32_t)des.ge[1].hwsp_ggtt + 0x180u &&
						px.hwsp_cpu == &des.ge[1].hwsp[0x60u],
						"p7: P7-PXP pxp_init_full: a pinned 4 KiB context on the first VCS, HWS_PXP timeline, streaming page");
					parity_intel_pxp_fini(&px, &gm);
					KCHECK(gm.objects_live == live1 && !px.inited && px.stream_cmd == 0,
						"p7: P7-PXP-FINI destroy_vcs_context + streaming page released");
					parity_intel_engines_release(&des, &gm);
				} else {
					KCHECK(0, "p7: P7-PXP intel_engines_init with a VCS failed");
				}
				dg.num_engines = 1u;
			}


			/* ====== EU test batch: the L-C1 words ====== */
			{
				static uint32_t cmds[1024];
				unsigned n = parity_eu_test_build_batch(cmds, 1024u, PARITY_EU_SHARED_VA,
					PARITY_EU_SHARED_VA, 559u);
				unsigned k, walker = 0u, sba = 0u, sel3d = 0u, selgpgpu = 0u, midl = 0u, vfe = 0u;

				for (k = 0u; k + 14u < n; k++) {
					if (cmds[k] == 0x7105000du && cmds[k + 7u] == 1u && cmds[k + 10u] == 1u &&
					    cmds[k + 12u] == 1u && cmds[k + 13u] == 1u && cmds[k + 14u] == 0xffffffffu)
						walker = k;
					if (cmds[k] == 0x61010014u && cmds[k + 3u] == (6u << 16) &&
					    cmds[k + 4u] == (1u | (6u << 4) | 0x00400000u) && cmds[k + 5u] == 1u &&
					    cmds[k + 10u] == (1u | (4u << 4) | 0x00400000u) && cmds[k + 11u] == 1u)
						sba = k;
					if (cmds[k] == 0x61041310u) sel3d = k;
					if (cmds[k] == 0x61041312u) selgpgpu = k;
					if (cmds[k] == 0x70020002u && cmds[k + 2u] == 32u && cmds[k + 3u] == 896u) midl = k;
					if (cmds[k] == 0x70000007u && cmds[k + 3u] == ((559u << 16) | (2u << 8))) vfe = k;
				}
				KCHECK(n > 0u && n < 1024u && sel3d < sba && sba < selgpgpu && selgpgpu < vfe &&
					vfe < midl && midl < walker && cmds[n - 2u] == PARITY_MI_BATCH_BUFFER_END,
					"eu: EU-BATCH 3D select, SBA (stateless MOCS 6, instruction base @0x100400000), GPGPU select, VFE(559), MIDL(896), walker(1x1x1 SIMD8), BB_END");
			}

			parity_gt_ppgtt_destroy(&gm, &pp);
			parity_gt_mem_fini(&gm);
			KCHECK(gm.objects_live == 0u && gm.allocated_pages == 0u &&
				pp.inited == 0 && pp.top_pd == 0,
				"p6c0: P6C0-OBJ teardown releases every object and its GGTT run");
			(void)drv_dma_device_destroy(c0_dma);
		}
	}

	kern_logf("i915: parity ktest (completion+workqueue): %d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? -1 : 0;
}
