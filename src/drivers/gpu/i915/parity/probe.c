/*
 * WS031 Linux-parity attach — P0..P2 diagnostic walk (see parity.h).
 *
 * Runs the reference probe order through the OS adaptation layer, records every
 * step in a trace, stops at the requested stage or at the first unimplemented
 * dependency (BLOCKED), and always tears down what it acquired.  It never
 * publishes the device and never calls the legacy attach.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <string.h>
#include <drivers/dma.h>
#include "parity.h"
#include "backend.h"
#include "pte.h"
#include "ktest.h"
#include "pcode.h"
#include "dram_bw.h"
#include "drm_device.h"
#include "bios.h"
#include "dp/parity_dp_kernel.h"
#include "lcd/lcd_hw_check.h"
#include "lcd/parity_lcd_kernel.h"
#include "lcd/parity_lcd_show.h"
#include "vga.h"
#include "power_domains.h"
#include "cdclk.h"
#include "display_core.h"
#include "dmc.h"
#include "display_state.h"
#include "irq.h"
#include "pch.h"
#include "display_nogem.h"
#include "gt_mmio.h"
#include "gt_init.h"
#include "gt_mem.h"
#include "gt_resume.h"
#include "gt_defaults.h"
#include "gt_verify_wa.h"
#include "gt_migrate.h"
#include "pxp.h"
#include "driver_probe.h"
#include "eu_test.h"
#include "native_precheck.h"
#include "lcd/parity_lcd_modeset.h"
#include "../linux/i915-ids.inc"   /* the id lists the PCI driver matches on */
#include "lcd/parity_hotplug.h"
/* E-123: the hotplug path is running (started after intel_display_driver_probe) */
static int parity_hpd_started;
#include "lcd/opregion_fwtest.h"
#include "../draw_fixture.h"
#include "reset.h"
#include "backend_sync.h"
#include "reset.h"
#include "wait.h"
#include <errno.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include "osdep/pci.h"
#include "osdep/mmio.h"
#include "osdep/dma.h"
#include "osdep/runtime_pm.h"
#include "osdep/trace.h"
#include <hal/hal.h>

static void
parity_dump_trace(struct osdep_trace *t)
{
	static struct osdep_trace_record buf[256];
	uint32_t n;
	uint32_t i;

	n = osdep_trace_snapshot(t, buf, 256U);
	for (i = 0U; i < n; i++)
		kern_logf("i915: parity tr[%u] %s %s a0=0x%llx a1=0x%llx\n",
			buf[i].seq, osdep_trace_op_name(buf[i].op),
			buf[i].what != 0 ? buf[i].what : "?",
			(unsigned long long)buf[i].arg0,
			(unsigned long long)buf[i].arg1);
	if (t->dropped != 0U)
		kern_logf("i915: parity trace dropped=%u (records overflowed)\n", t->dropped);
}

/* N0 (E-120): the aperture, and the reference's readout gate for the precheck (no write) */
static uint64_t n0_gmadr_base, n0_gmadr_size;

/*
 * Which display this device has.  The reference keeps this in its device-info table; here the two
 * platforms this driver carries display data for are told apart by the PCI device id, using the SAME
 * id lists the PCI driver matches on (linux/i915-ids.inc).
 *   Tiger Lake        display version 12
 *   Alder Lake-P/N, Raptor Lake-P/U  display version 13 (XE_LPD)
 */
#define I915_ID_VALUE(product) (product)
static int devid_is_tigerlake(uint16_t devid)
{
	static const uint16_t tgl[] = { INTEL_TGL_IDS(I915_ID_VALUE) };
	unsigned i;

	for (i = 0u; i < sizeof(tgl) / sizeof(tgl[0]); i++)
		if (tgl[i] == devid)
			return 1;
	return 0;
}
static const struct parity_opregion_data *p2_opd;   /* E-122: the P2 OpRegion data (read-only acquisition) */
static struct osdep_mmio *n0_mmio;
static int n0_pipe_powered(void *ctx, unsigned pipe)
{
	struct parity_power_domains *pd = ctx;

	/* the wells' real STATE (before init_hw the driver neither synced nor took over the firmware's requests) */
	return parity_power_domain_hw_state_on(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pipe), n0_mmio) &&
		parity_power_domain_hw_state_on(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + pipe), n0_mmio);
}

static const char *
outcome_name(enum parity_outcome o)
{
	switch (o) {
	case PARITY_STOPPED: return "STOPPED";
	case PARITY_BLOCKED: return "BLOCKED";
	case PARITY_FAILED:  return "FAILED";
	default:             return "?";
	}
}

static unsigned
popcount32(uint32_t v)
{
	unsigned n = 0U;
	while (v != 0U) { n += v & 1U; v >>= 1; }
	return n;
}

/* CPU physical-address width from CPUID 0x80000008 (amd64 diagnostic). */
static unsigned
parity_cpu_phys_bits(void)
{
	uint32_t eax = 0x80000000U, ebx, ecx, edx;

	__asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
	if (eax >= 0x80000008U) {
		eax = 0x80000008U;
		__asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
		return (unsigned)(eax & 0xffU);
	}
	return 36U;
}


int
drv_i915_parity_attach(struct i915_device *device, enum parity_stage stop_after,
		       struct parity_result *out)
{
	static struct osdep_trace trace;
	static struct osdep_pci pci;
	static struct osdep_mmio mmio;
	static struct osdep_rpm rpm;
	static struct osdep_dma_device dma;
	struct parity_pci_priv pci_priv;
	struct parity_mmio_priv mmio_priv;
	struct parity_result res;
	struct drv_pci_bar bar;
	struct drv_dma_device *dma39 = 0;
	struct drv_dma_vector *scratch_vec = 0;
	uint64_t scratch_pte = 0;
	int pci_enabled = 0;
	int bar_claimed = 0;
	int bar_mapped = 0;
	int gtt_mapped = 0;
	int dma_created = 0;
	int scratch_created = 0;
	int msi_kept = 0;
	void *wc_aperture_va = 0;
	uint64_t wc_aperture_size = 0;
	struct spinlock uncore_lock;
	struct mutex sb_lock;
	struct parity_dram_info dram_info;
	struct parity_bw_state bw_state;
	static struct parity_drm_device drm_dev;   /* large (per-pipe workers); attach is one-at-a-time */
	int drm_inited = 0;
	static struct parity_vbt_state vbt_state;
	int opregion_vbt_present = 0;   /* set only if P2 held a usable OpRegion VBT */
	static struct parity_vga_client vga_client;
	static struct parity_power_domains power_domains;
	static struct parity_pmdemand pmdemand;
	static struct parity_cdclk_dev cdclk;
	static struct parity_pw_ctx pwc;
	static struct parity_edp_device edp_dev;    /* the resident panel: outlives setup_outputs, released in teardown */
	static struct parity_display_core dcore;
	static struct parity_dmc_dev dmc_dev;
	static struct parity_display_state dstate;
	static struct parity_irq_dev irqdev;
	static struct parity_irq_vblank irq_vblank;
	static struct parity_pch_state pch;
	static struct parity_display_nogem nogem;
	static struct parity_gt_mmio gtmmio;
	static struct parity_gt_init gtinit;
	static struct parity_gt_mem gtmem;
	static struct parity_gt_ppgtt gtpp;
	static struct parity_gt_engines gteng;
	static struct parity_gt_defaults gtdef;
	static struct parity_gt_verify_wa gtvwa;
	static struct parity_gt_migrate gtmig;
	static struct parity_pxp pxp;
	static struct parity_driver_probe dprobe;
	static struct parity_eu_test eutest;
	int eutest_inited = 0;
	static struct parity_draw_test drawtest;
	int drawtest_inited = 0;
	static struct parity_r1_test r1test;
	int r1test_inited = 0;
	static struct parity_tex_test textest;
	int textest_inited = 0;
	static struct parity_t3_test t3test;
	int t3test_inited = 0;
	int gtvwa_inited = 0;
	int gtmig_inited = 0;
	int pxp_inited = 0;
	int opregion_present = 0;
	int gteng_resumed = 0;
	int gtdef_inited = 0;
	struct parity_gt_object *gt_scratch = 0;
	int gtmem_inited = 0;
	int gteng_inited = 0;
	unsigned ggtt_entries = 0u;   /* P2's PTE window size, kept for P6 */
	static struct parity_kworkqueue dmc_wq, modeset_wq, flip_wq;
	int vga_registered = 0;
	int power_domains_inited = 0;
	int display_core_inited = 0;
	int dmc_inited = 0;
	int dstate_inited = 0;
	int irq_installed = 0;
	int nogem_inited = 0;
	int modeset_wq_ok = 0, flip_wq_ok = 0;
	unsigned display_ver = 13u;   /* set from the PCI device id below */
	int is_alderlake_p = 1;
	static struct osdep_rpm probe_pm;   /* PCI-core probe runtime PM (distinct ref) */
	int probe_pm_held = 0;
	int rc;

	res.reached = PARITY_STAGE_NONE;
	res.outcome = PARITY_STOPPED;
	res.error = 0;
	res.where = "start";
	res.lcd_test_ran = 0; res.lcd_test_pass = 0; res.lcd_cleanup_rc = 0; res.lcd_retained = 0;
	res.lcd_first_anomaly_stage = "-";
	res.last_completed = "";

	/* Device-owned locks + display bandwidth state (device lifetime = this attach). */
	spin_init(&uncore_lock, LOCK_RANK_DEVICE, "parity-uncore");
	(void)mutex_init(&sb_lock, LOCK_RANK_DEVICE, "parity-sb");
	memset(&dram_info, 0, sizeof(dram_info));
	memset(&bw_state, 0, sizeof(bw_state));

	/* Real-time waits need a monotonic counter; its absence is a time-base
	 * anomaly, not a normal probe condition. */
	{
		if (!parity_wait_time_base_ok()) {
			res.outcome = PARITY_FAILED; res.error = 5 /* EIO */;
			res.where = "time_base_anomaly"; goto teardown;
		}
	}

	osdep_trace_init(&trace);
	pci_priv.pci = device->pci;
	pci_priv.msi_irq = -1;
	osdep_pci_init(&pci, parity_pci_backend(), &pci_priv, &trace);

	{
		uint16_t devid = osdep_pci_read16(&pci, 0x02u);

		if (devid_is_tigerlake(devid)) {
			display_ver = 12u;
			is_alderlake_p = 0;
		}
		kern_logf("i915: parity display device: 8086:%04x -> display version %u, %s\n", devid,
			display_ver, is_alderlake_p ? "Alder Lake-P class (XE_LPD)" : "Tiger Lake");
	}
	kern_logf("i915: parity attach begin (stop_after=P%d)\n", (int)stop_after - 1);

	/*
	 * PCI-core probe runtime PM (local_pci_probe equivalent): take a runtime PM
	 * reference and resume the device to D0 BEFORE the driver P0 work.  This is a
	 * THIRD reference, distinct from the device-lifetime reference the runner
	 * holds and from the i915 runtime_pm struct initialised at P0.3.  get_sync
	 * leaves the usage count incremented even if resume fails, so the teardown
	 * path always releases it; it is released here only because the diagnostic
	 * never publishes -- a real publish would transfer the reference instead.
	 */
	osdep_rpm_init_early(&probe_pm, parity_pci_probe_pm_backend(), &pci, &trace);
	{
		int pmrc = osdep_rpm_get_sync(&probe_pm);   /* usage++, resume to D0 */

		probe_pm_held = 1;
		osdep_trace_emit(&trace, PARITY_STAGE_P0,
			pmrc == 0 ? OSDEP_TR_ACQUIRE : OSDEP_TR_FAIL, "pci_probe_runtime_pm_get_sync",
			(uint64_t)(unsigned)osdep_rpm_usage(&probe_pm), (uint64_t)(unsigned)(-pmrc));
		kern_logf("i915: parity PM probe get_sync: cfg_vendor=0x%04x usage=%d active=%d "
			"resume_rc=%d\n", osdep_pci_read16(&pci, 0x00u), osdep_rpm_usage(&probe_pm),
			osdep_rpm_active(&probe_pm), pmrc);
		if (pmrc < 0) {
			/* Resume is implemented (a real D0 transition) -- a failure is that
			 * error, not a stub; the reference is released in teardown. */
			res.outcome = PARITY_FAILED; res.error = -pmrc;
			res.where = "pci_probe_runtime_pm"; goto teardown;
		}
	}

	/*
	 * Work 2 (parity_sync_ktest) is intentionally NOT run from here: the i915
	 * attach runs in the boot device-probe context, where — as the NVMe driver
	 * documents — a timer-driven wait-queue wakeup can stall (regular threads /
	 * timer delivery are not yet in their steady state).  The ktest belongs in a
	 * fully-up context (a GPU-free boot hook); running it here hangs the timeout
	 * path.  K1 (non-sleeping completion) was verified to pass here.
	 */

	/* =============================== P0 =============================== */
	res.reached = PARITY_STAGE_P0;

	/* P0.2 pci_enable_device (resource-aware, refcounted) — real config access. */
	rc = osdep_pci_enable_device(&pci);
	if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "pci_enable_device"; goto teardown; }
	pci_enabled = 1;
	kern_logf("i915: parity P0 pci_enable_device ok (command=0x%04x)\n",
		osdep_pci_read16(&pci, OSDEP_PCI_COMMAND));

	/* P0.3 i915_driver_create: init device runtime-PM early (struct only, not enable). */
	osdep_rpm_init_early(&rpm, parity_rpm_backend(), device, &trace);
	osdep_trace_emit(&trace, PARITY_STAGE_P0, OSDEP_TR_NOTE, "i915_driver_create", 0U, 0U);

	/* P0.4 early_probe: locks/timers/workqueues exist in zedBSD; nothing to allocate here. */
	osdep_trace_emit(&trace, PARITY_STAGE_P0, OSDEP_TR_NOTE, "i915_driver_early_probe", 0U, 0U);

	/* P0.5 vgpu_detect: physical passthrough -> the non-vGPU branch (active=false). */
	osdep_trace_emit(&trace, PARITY_STAGE_P0, OSDEP_TR_NOTE, "intel_vgpu_detect:physical", 0U, 0U);

	/* P0.6 gt_probe_all: single GT; its structures are the engine set (already present). */
	osdep_trace_emit(&trace, PARITY_STAGE_P0, OSDEP_TR_NOTE, "intel_gt_probe_all:single_gt", 0U, 0U);

	if (stop_after == PARITY_STAGE_P0) { res.outcome = PARITY_STOPPED; res.where = "end_of_P0"; goto teardown; }

	/* =============================== P1 =============================== */
	res.reached = PARITY_STAGE_P1;

	/* P1.2 intel_uncore_init_mmio: map the register half of BAR0. */
	rc = drv_pci_device_bar(device->pci, GEN4_GTTMMADR_BAR, &bar);
	if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "pci_bar"; goto teardown; }
	rc = drv_pci_device_claim_bar(device->pci, GEN4_GTTMMADR_BAR);
	if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "claim_bar"; goto teardown; }
	bar_claimed = 1;
	rc = drv_pci_device_map_bar_region(device->pci, GEN4_GTTMMADR_BAR, 0U,
		(size_t)(bar.size / 2U), DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
		&device->regs);
	if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "map_bar"; goto teardown; }
	bar_mapped = 1;
	mmio_priv.base = device->regs.address;
	mmio_priv.size = (unsigned long)device->regs.size;
	osdep_mmio_init(&mmio, parity_mmio_backend(), &mmio_priv,
		parity_mmio_ranges, parity_mmio_range_count, &trace);
	osdep_trace_emit(&trace, PARITY_STAGE_P1, OSDEP_TR_ACQUIRE, "uncore_mmio_bar",
		(uint64_t)device->regs.size, 0U);
	kern_logf("i915: parity P1 uncore BAR mapped (%llu bytes)\n",
		(unsigned long long)device->regs.size);

	/* P1.3 intel_device_info_runtime_init: read the slice/DSS fuses (real MMIO+forcewake). */
	{
		uint32_t slice, dss;
		unsigned slices, dss_count;

		rc = osdep_fw_get(&mmio, OSDEP_FW_GT);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "forcewake_gt"; goto teardown; }
		slice = osdep_mmio_read32(&mmio, 0x9138U);   /* GEN11_GT_SLICE_ENABLE */
		dss = osdep_mmio_read32(&mmio, 0x913cU);      /* GEN12 DSS fuse */
		osdep_fw_put(&mmio, OSDEP_FW_GT);

		slices = popcount32(slice & 0xffU);
		dss_count = popcount32(dss);
		kern_logf("i915: parity P1 device_info: slice_fuse=0x%08x(%u) dss_fuse=0x%08x(%u DSS)\n",
			slice, slices, dss, dss_count);
	}

	/* P1.4 intel_gt_init_mmio: point MCR at multicast (GEN8_MCR_SELECTOR, always-on). */
	osdep_mmio_write32(&mmio, 0x0fdcU, 0x80000000U);   /* GEN11_MCR_MULTICAST */
	osdep_trace_emit(&trace, PARITY_STAGE_P1, OSDEP_TR_NOTE, "intel_gt_init_mmio:mcr_multicast", 0U, 0U);

	/*
	 * P1.4b intel_gt_init_mmio, the rest: the timestamp clock, the FULL SSEU
	 * decode, the MCR L3BANK steering mask, the engine objects (with the media
	 * fuses applied) and the fault check.  P6 depends on all of it.
	 */
	{
		unsigned gi;

		/*
		 * The fuse and clock registers this reads (0x9118/0x9134/0x9138/
		 * 0x913c/0x9140, RPM_CONFIG0, CTC_MODE) all live in a FORCEWAKE_GT
		 * range: read without forcewake they return all-ones, which decodes
		 * as "everything fused off".  The reference gets this for free from
		 * intel_uncore's per-register forcewake; here it is explicit.
		 */
		rc = osdep_fw_get(&mmio, OSDEP_FW_GT);
		if (rc == 0)
			rc = osdep_fw_get(&mmio, OSDEP_FW_RENDER);
		if (rc != 0) {
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = "forcewake_gt_init_mmio"; goto teardown;
		}
		for (gi = 0u; gi < sizeof(gtmmio); gi++) ((char *)&gtmmio)[gi] = 0;
		(void)parity_intel_gt_init_mmio(&gtmmio, 12 /* GRAPHICS_VER */,
			(1u << 0) | (1u << 1) | (1u << 8) | (1u << 10) | (1u << 16),
			&mmio);   /* ADL-P platform_engine_mask: RCS0|BCS0|VCS0|VCS2|VECS0 */
		osdep_fw_put(&mmio, OSDEP_FW_RENDER);
		osdep_fw_put(&mmio, OSDEP_FW_GT);
		kern_logf("i915: parity P1 gt_init_mmio: clock=%uHz period=%uns "
			"sseu(slice=0x%x dss=0x%x eu/ss=%u total=%u) l3bank=0x%x "
			"engine_mask=0x%x engines=%u fault=0x%x\n",
			gtmmio.clock_frequency, gtmmio.clock_period_ns,
			gtmmio.sseu.slice_mask, gtmmio.sseu.subslice_mask,
			gtmmio.sseu.eu_per_subslice, gtmmio.sseu.eu_total,
			gtmmio.l3bank_mask, gtmmio.engine_mask, gtmmio.num_engines,
			gtmmio.fault_reg);
		for (gi = 0u; gi < gtmmio.num_engines; gi++)
			kern_logf("i915: parity P1 engine[%u] %s: class=%d inst=%d "
				"base=0x%05x reset_domain=0x%x ctx_size=%u caps=0x%x\n",
				gi, gtmmio.engines[gi].name, gtmmio.engines[gi].class,
				gtmmio.engines[gi].instance, gtmmio.engines[gi].mmio_base,
				gtmmio.engines[gi].reset_domain,
				gtmmio.engines[gi].context_size,
				gtmmio.engines[gi].uabi_capabilities);
		res.last_completed = "intel_gt_init_mmio";
	}


	/*
	 * P1.5 sanitize_gpu -> __intel_gt_reset(ALL_ENGINES): the full reset control
	 * flow lives in reset.c (testable against a mock MMIO backend).
	 */
	osdep_trace_emit(&trace, PARITY_STAGE_P1, OSDEP_TR_NOTE,
		"reset_callback:gen8_reset_engines", 0U, 0U);
	rc = parity_gt_reset_all(&uncore_lock, &mmio, 2000u);
	if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "gt_reset"; goto teardown; }
	osdep_trace_emit(&trace, PARITY_STAGE_P1, OSDEP_TR_ACQUIRE,
		"sanitize_gpu:__intel_gt_reset", 0x1U, 0U);
	kern_logf("i915: parity P1 sanitize_gpu __intel_gt_reset(ALL_ENGINES) ok\n");

	if (stop_after == PARITY_STAGE_P1) { res.outcome = PARITY_STOPPED; res.where = "end_of_P1"; goto teardown; }

	/* =============================== P2 =============================== */
	res.reached = PARITY_STAGE_P2;

	/*
	 * P2.1 i915_set_dma_info: request the reference's 39-bit DMA mask (ADL-P
	 * dma_mask_size=39) and UINT_MAX max segment.  The shared PCAT bus DMA device is
	 * a conservative 32-bit default (pci-pcat.c) — leaving it would BOUNCE i915's
	 * 39-bit-allocated pages (I915_DMA_MAX_ADDRESS).  i915 declares its own 39-bit
	 * capability (like dma_set_mask), so <=39-bit pages map identity without bouncing.
	 * The requested value is NOT derived from the backend's current 32.
	 */
	{
		static const struct drv_dma_constraints i915_dma_cons = {
			39U,           /* address_bits: DMA_BIT_MASK(39) */
			0xffffffffU,   /* max_segment_size: UINT_MAX, as Linux requests */
			0U,            /* segment_boundary */
			1              /* coherent */
		};
		struct drv_dma_device *bus_dma = drv_pci_device_dma(device->pci);

		rc = drv_dma_device_create(&i915_dma_cons, &dma39);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "dma_device_create"; goto teardown; }
		dma_created = 1;
		device->dma = dma39;

		osdep_dma_device_init(&dma, parity_dma_backend(), dma39, &trace);
		rc = osdep_dma_set_info(&dma, 39U, 0xffffffffU);   /* requested = 39-bit / UINT_MAX */
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "set_dma_info"; goto teardown; }

		kern_logf("i915: parity P2 dma_request: streaming_mask=0x%llx coherent_mask=0x%llx max_seg=0x%x\n",
			(unsigned long long)((((uint64_t)1) << 39) - 1U),
			(unsigned long long)((((uint64_t)1) << 39) - 1U), 0xffffffffU);
		kern_logf("i915: parity P2 dma_backend: bus_bits=%u i915_bits=%u i915_max_seg=0x%llx coherent=%d\n",
			bus_dma != 0 ? drv_dma_device_address_bits(bus_dma) : 0U,
			drv_dma_device_address_bits(dma39),
			(unsigned long long)drv_dma_device_max_segment_size(dma39),
			drv_dma_device_is_coherent(dma39));
	}

	/* P2.2 i915_ggtt_probe_hw: size the GGTT from GMCH control, map its table window. */
	{
		uint16_t gmch = osdep_pci_read16(&pci, 0x50U);        /* SNB_GMCH_CTRL */
		unsigned ggms = ((unsigned)gmch >> 6) & 0x3U;          /* BDW_GMCH_GGMS */
		uint64_t table_bytes;
		uint64_t window_bytes;
		unsigned entries;

		if (ggms == 0U) { res.outcome = PARITY_FAILED; res.error = -19; res.where = "ggtt_disabled"; goto teardown; }
		table_bytes = (uint64_t)(1U << ggms) << 20;
		rc = drv_pci_device_bar(device->pci, GEN4_GTTMMADR_BAR, &bar);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "ggtt_bar"; goto teardown; }
		window_bytes = bar.size / 2U;
		if (table_bytes > window_bytes)
			table_bytes = window_bytes;
		rc = drv_pci_device_map_bar_region(device->pci, GEN4_GTTMMADR_BAR, window_bytes,
			(size_t)table_bytes, DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
			&device->gtt);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "ggtt_map"; goto teardown; }
		gtt_mapped = 1;
		entries = (unsigned)(table_bytes / 8U);
		ggtt_entries = entries;
		osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_ACQUIRE, "ggtt_table_window",
			table_bytes, (uint64_t)entries);
		kern_logf("i915: parity P2 ggtt_probe: ggms=%u entries=%u (%llu MiB GPU VA window)\n",
			ggms, entries, (unsigned long long)((uint64_t)entries * I915_PAGE_BYTES >> 20));

		/*
		 * setup_scratch_page (inside ggtt_probe, as in the reference): an internal
		 * object via drv_dma_vector — backing pages + SG + DMA mapping — whose
		 * mapped DMA address (39-bit device, identity here) encodes the scratch PTE.
		 * Not alloc_coherent; not a fabricated address.  The table itself is NOT
		 * filled here (clear_range/scratch_range are separate init_hw callbacks).
		 */
		rc = drv_dma_vector_create(dma39, I915_PAGE_BYTES, &scratch_vec);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "scratch_alloc"; goto teardown; }
		scratch_created = 1;
		{
			void *scpu = drv_dma_vector_address(scratch_vec);
			unsigned segn = drv_dma_vector_count(scratch_vec);
			struct drv_dma_segment seg;
			uint64_t mask = (((uint64_t)1) << 39) - 1U;

			/* One page must map as one contiguous segment. */
			if (scpu == 0 || segn != 1U) { res.outcome = PARITY_FAILED; res.error = -22; res.where = "scratch_layout"; goto teardown; }
			memset(scpu, 0, I915_PAGE_BYTES);   /* init content: zeroed scratch */
			rc = drv_dma_vector_segment(scratch_vec, 0U, &seg);
			if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "scratch_segment"; goto teardown; }
			/* Range-check + encode the GGTT (not PPGTT) PTE; refuse rather than mask. */
			if (!parity_ggtt_pte_encode(osdep_dma_addr(seg.address), (uint64_t)seg.length, mask, &scratch_pte)) {
				res.outcome = PARITY_FAILED; res.error = -22; res.where = "scratch_pte_range"; goto teardown;
			}
			osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_MAP, "scratch_page", seg.address, scratch_pte);
			kern_logf("i915: parity P2 scratch: cpu=0x%llx dma=0x%llx len=0x%llx ggtt_pte=0x%llx\n",
				(unsigned long long)(uintptr_t)scpu, (unsigned long long)seg.address,
				(unsigned long long)seg.length, (unsigned long long)scratch_pte);
		}
	}

	/*
	 * P2.3 i915_ggtt_init_hw: address-space init, CPU mappable aperture (GMADR),
	 * and fence registers.  Separate resources from the GGTT PTE window / scratch.
	 */
	{
		struct drv_pci_bar aperture;
		uint64_t gmadr_start = 0u, mappable_end = 0u;
		unsigned num_fences = 32u;   /* Gen12 (>= Gen7): 32 fence registers */
		unsigned fi;

		/* i915_address_space_init(VM_CLASS_GGTT): the GGTT VM covers the whole GTT. */
		osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "address_space_init:GGTT",
			(uint64_t)device->gtt.size * 512u, 0u);

		/*
		 * CPU mappable aperture = GMADR (BAR2).  The reference uses io_mapping_init_wc,
		 * a LAZY per-page WC facility: record gmadr.start + mappable_end (the mappable
		 * range) rather than eagerly mapping the whole aperture.  zedBSD has no WC map
		 * flag; the WC/MTRR step is recorded as not-available rather than faked.
		 */
		rc = drv_pci_device_bar(device->pci, GEN4_GMADR_BAR, &aperture);
		if (rc == 0 && aperture.size != 0u) {
			unsigned pbits = parity_cpu_phys_bits();
			uint64_t limit = (pbits >= 64u) ? ~(uint64_t)0 : ((((uint64_t)1) << pbits) - 1u);
			uint32_t bar2 = osdep_pci_read32(&pci, 0x18u);   /* GMADR low */
			uint32_t bar3 = osdep_pci_read32(&pci, 0x1cu);   /* GMADR high (64-bit BAR) */

			gmadr_start = aperture.bus_address;
			mappable_end = aperture.size;
			n0_gmadr_base = gmadr_start;
			n0_gmadr_size = mappable_end;
			kern_logf("i915: parity env: phys_bits=%u cpu_limit=0x%llx bar2_raw=0x%08x bar3_raw=0x%08x\n",
				pbits, (unsigned long long)limit, bar2, bar3);
			kern_logf("i915: parity P2 ggtt_init_hw: gmadr=0x%llx mappable_end=0x%llx\n",
				(unsigned long long)gmadr_start, (unsigned long long)mappable_end);

			/* Validate the aperture as a CPU-mappable MMIO range before any WC map. */
			if (gmadr_start > limit || (mappable_end - 1u) > (limit - gmadr_start)) {
				kern_logf("i915: parity ggtt_init_hw: aperture [0x%llx+0x%llx) exceeds CPU phys limit 0x%llx; not CPU-mappable\n",
					(unsigned long long)gmadr_start, (unsigned long long)mappable_end,
					(unsigned long long)limit);
				res.outcome = PARITY_FAILED; res.error = -22; res.where = "ggtt_aperture_range"; goto teardown;
			}
			osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_ACQUIRE, "ggtt_aperture_gmadr",
				gmadr_start, mappable_end);

			/*
			 * io_mapping_init_wc equivalent: map the WHOLE mappable aperture
			 * (gmadr.start .. mappable_end) write-combining and hold it for the
			 * device's P2 lifetime; the teardown releases the same range.  This is a
			 * CPU view of the existing BAR range -- no aperture-sized RAM is
			 * allocated, only the page tables backing the view.
			 */
			{
				void *wc_va = NULL;
				int wc_rc = hal_space_map_device((hal_physaddr_t)gmadr_start,
					(size_t)mappable_end, HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_WC, &wc_va);
				if (wc_rc != HAL_OK || wc_va == NULL) {
					kern_logf("i915: parity ggtt_init_hw: WC aperture map(base=0x%llx size=0x%llx) failed rc=%d\n",
						(unsigned long long)gmadr_start, (unsigned long long)mappable_end, wc_rc);
					res.outcome = PARITY_FAILED; res.error = wc_rc; res.where = "ggtt_aperture_wc"; goto teardown;
				}
				wc_aperture_va = wc_va;
				wc_aperture_size = mappable_end;
				kern_logf("i915: parity P2 ggtt_init_hw: WC aperture mapped base=0x%llx requested_size=0x%llx mapped_size=0x%llx va=%p attr=RW|WC\n",
					(unsigned long long)gmadr_start, (unsigned long long)mappable_end,
					(unsigned long long)wc_aperture_size, wc_va);
				osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_ACQUIRE, "ggtt_aperture_wc", gmadr_start, mappable_end);
			}
		}

		/* intel_ggtt_init_fences: initialise (clear) the fence registers under forcewake. */
		rc = osdep_fw_get(&mmio, OSDEP_FW_GT);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "forcewake_fences"; goto teardown; }
		for (fi = 0u; fi < num_fences; fi++) {
			osdep_mmio_raw_write32(&mmio, 0x100000u + fi * 8u, 0u);        /* FENCE_REG_GEN6_LO(fi) */
			osdep_mmio_raw_write32(&mmio, 0x100000u + fi * 8u + 4u, 0u);   /* FENCE_REG_GEN6_HI(fi) */
		}
		osdep_fw_put(&mmio, OSDEP_FW_GT);
		osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "intel_ggtt_init_fences", num_fences, 0u);
		kern_logf("i915: parity P2 ggtt_init_hw: %u fence registers initialized\n", num_fences);
	}

	/* P2.4 intel_gt_tiles_init: ADL-P is a single-tile part. */
	osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "intel_gt_tiles_init:single_tile", 1u, 0u);

	/* P2.5 intel_memory_regions_hw_probe: system memory (SMEM) region. */
	osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "memory_regions_hw_probe:smem", 0u, 0u);

	/* P2.6 i915_ggtt_enable_hw: a no-op from Gen6 on (reproduce the branch, write nothing). */
	osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "ggtt_enable_hw:gen6plus_noop", 0u, 0u);

	/* P2.7 pci_set_master: enable bus mastering AFTER GGTT enable (real config write). */
	osdep_pci_set_bus_master(&pci, 1);
	osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_ACQUIRE, "pci_set_master",
		osdep_pci_read16(&pci, OSDEP_PCI_COMMAND), 0u);
	kern_logf("i915: parity P2 pci_set_master ok (command=0x%04x)\n",
		osdep_pci_read16(&pci, OSDEP_PCI_COMMAND));

	/*
	 * P2.8 pci_enable_msi: acquire the vector, program the PCI MSI message, and
	 * enable it (resource-only; attaching a handler is a P4 concern).  As the
	 * end-of-P2 diagnostic, immediately stop the source and release the vector.
	 */
	rc = osdep_pci_setup_msi(&pci);
	if (rc != 0) {
		kern_logf("i915: parity P2 pci_enable_msi failed rc=%d\n", rc);
		res.outcome = PARITY_FAILED; res.error = rc; res.where = "pci_enable_msi"; goto teardown;
	}
	kern_logf("i915: parity P2 pci_enable_msi ok (vector programmed, msi_enabled=%d, handler unattached)\n",
		osdep_pci_msi_enabled(&pci));
	if (stop_after == PARITY_STAGE_P2) {
		/* Diagnostic stop at P2: stop the source and release the vector now. */
		osdep_pci_teardown_msi(&pci);
		kern_logf("i915: parity P2 pci_enable_msi torn down (source stopped, vector freed)\n");
	} else {
		/* Continue mode: keep the MSI resource for P4 (handler attach). */
		msi_kept = 1;
		kern_logf("i915: parity P2 pci_enable_msi kept for P4 (resource retained)\n");
	}

	/*
	 * P2.9 intel_opregion_setup: ASLS (PCI cfg 0xFC) holds the OpRegion base.
	 * Map the 8 KiB region and validate the "IntelGraphicsMem" signature +
	 * version, reproducing the reference branch (QEMU x-igd-opregion=on sets
	 * ASLS and publishes the OpRegion).  ASLS==0 is the legitimate -ENOTSUPP
	 * (absent) branch.
	 */
	{
		uint32_t asls = osdep_pci_read32(&pci, 0xFCu);

		kern_logf("i915: parity P2 opregion: ASLS=0x%08x\n", asls);
		/* E-122 VBT_ONLY: the OpRegion as data (read-only copy + VBT); the runtime protocol is not joined */
		{
			static struct parity_opregion_data opd;

			(void)parity_opregion_read_data(asls, &opd);
			parity_opregion_log(&opd);
			p2_opd = &opd;
			if (opd.vbt_valid) {
				parity_bios_set_opregion_vbt(opd.vbt, opd.vbt_size);
				opregion_vbt_present = 1;
			}
		}
		opregion_present = asls != 0u;
		if (asls == 0u) {
			osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "intel_opregion_absent", 0u, 0u);
			/* ASLS==0: no OpRegion, hence no OpRegion VBT (opregion_vbt_present stays 0). */
		} else {
			void *op = NULL;
			int op_rc = hal_space_map_device((hal_physaddr_t)asls, 0x2000u,
				HAL_SPACE_READ, &op);
			if (op_rc != HAL_OK || op == NULL) {
				kern_logf("i915: parity P2 opregion: map(ASLS=0x%08x,8KiB) rc=%d (not device-mappable)\n",
					asls, op_rc);
				osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "intel_opregion_map_unavailable",
					(uint64_t)asls, (uint64_t)(unsigned)op_rc);
			} else {
				static const char want[16] = {
					'I','n','t','e','l','G','r','a','p','h','i','c','s','M','e','m' };
				const volatile uint8_t *hdr = (const volatile uint8_t *)op;
				char sign[17];
				uint32_t size_kib;
				uint32_t over;
				unsigned i;
				int ok = 1;

				for (i = 0u; i < 16u; i++)
					sign[i] = (char)hdr[i];
				sign[16] = '\0';
				size_kib = *(const volatile uint32_t *)(const void *)(hdr + 0x10);
				over = *(const volatile uint32_t *)(const void *)(hdr + 0x14);
				kern_logf("i915: parity P2 opregion: sign='%s' size=%uKiB version=0x%08x\n",
					sign, size_kib, over);
				for (i = 0u; i < 16u; i++)
					if (sign[i] != want[i]) { ok = 0; break; }
				if (ok)
					osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_ACQUIRE, "intel_opregion_setup",
						(uint64_t)asls, (uint64_t)over);
				else {
					kern_logf("i915: parity P2 opregion: signature mismatch\n");
					osdep_trace_emit(&trace, PARITY_STAGE_P2, OSDEP_TR_NOTE, "intel_opregion_bad_signature",
						(uint64_t)asls, 0u);
				}
				(void)hal_space_unmap_device(op, 0x2000u);
			}
		}
	}

	/*
	 * P2.10 intel_dram_detect + P2.11 intel_bw_init_hw: hw_probe's DRAM info and
	 * display-bandwidth setup, via the common PCODE mailbox.  Both are void in
	 * the reference -- a tolerated PCODE/data failure is logged and probing
	 * continues; it is NOT an attach failure.
	 */
	{
		int dret;
		int bret;

		dret = parity_dram_detect(&sb_lock, &mmio, &dram_info);
		osdep_trace_emit(&trace, PARITY_STAGE_P2,
			dret == 0 ? OSDEP_TR_ACQUIRE : OSDEP_TR_NOTE,
			"intel_dram_detect", (uint64_t)(unsigned)dram_info.type,
			(uint64_t)dram_info.num_channels);

		bret = parity_bw_init_hw(&sb_lock, &mmio, &dram_info, &bw_state);
		osdep_trace_emit(&trace, PARITY_STAGE_P2,
			bret == 0 ? OSDEP_TR_ACQUIRE : OSDEP_TR_NOTE,
			"intel_bw_init_hw", (uint64_t)(unsigned)bw_state.sagv_status, 0u);
		kern_logf("i915: parity P2 hw_probe tail: dram_detect rc=%d bw_init rc=%d sagv=%d\n",
			dret, bret, bw_state.sagv_status);
	}

	/*
	 * End of P2 (i915_driver_hw_probe): DMA / GGTT / memory region / bus master /
	 * MSI / OpRegion / DRAM / bandwidth are all ported.  Stop cleanly at the P2
	 * boundary; the runner publishes nothing (published=0) and teardown reverses
	 * every P2 acquisition.
	 */
	if (stop_after == PARITY_STAGE_P2) {
		res.outcome = PARITY_STOPPED;
		res.where = "end_of_P2";
		goto teardown;
	}

	/* =============================== P3 =============================== */
	res.reached = PARITY_STAGE_P3;

	/*
	 * P3 intel_display_driver_probe_noirq: the display "noirq" bring-up in
	 * i915_driver_probe order -- drm_vblank_init, intel_bios_init,
	 * intel_vga_register, intel_power_domains_init(+_hw), intel_pmdemand_init_early,
	 * intel_dmc_init, the modeset/flip workqueues, intel_mode_config_init,
	 * intel_cdclk_init, intel_color_init, intel_dbuf_init, intel_bw_init (the
	 * display bandwidth SOFTWARE state, distinct from P2 intel_bw_init_hw),
	 * intel_pmdemand_init, intel_init_quirks, intel_fbc_init.  The P2 device
	 * state (WC aperture, MSI vector, MMIO, DRAM/bandwidth) is retained -- the
	 * final teardown, not this boundary, reverses it.
	 */
	osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_NOTE, "display_driver_probe_noirq:enter", 0u, 0u);
	kern_logf("i915: parity P3 display_driver_probe_noirq enter (device state carried from P2, msi_kept=%d)\n",
		msi_kept);

	/* i915_inject_probe_failure(): a debug-only no-op here. */
	osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_NOTE, "i915_inject_probe_failure:noop", 0u, 0u);
	/* HAS_DISPLAY(ADL-P) is true (independent of the absent OpRegion/ASLS). */
	osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_NOTE, "HAS_DISPLAY:true", 1u, 0u);

	/*
	 * P3.1 drm_dev_init + drm_vblank_init: bring up the DRM device management
	 * state and the per-CRTC vblank state.  INTEL_NUM_PIPES = hweight8(pipe_mask);
	 * ADL-P (xe_lpd) pipe_mask is A|B|C|D (0xf) -> 4 CRTCs.  The P2 device state
	 * (DRAM/bandwidth/MSI/WC) is retained; only display management is added.
	 */
	{
		unsigned pipe_mask = 0xfu;   /* ADL-P runtime pipe_mask (A|B|C|D) */
		unsigned num_pipes = popcount32(pipe_mask & 0xffu);

		rc = parity_drm_dev_init(&drm_dev, device, 0x3u /* MODESET | ATOMIC */);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "drm_dev_init"; goto teardown; }
		drm_inited = 1;
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_ACQUIRE, "drm_dev_init", 0u, 0u);

		rc = parity_drm_vblank_init(&drm_dev, num_pipes);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "drm_vblank_init"; goto teardown; }
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_ACQUIRE, "drm_vblank_init", (uint64_t)num_pipes, 0u);
		res.last_completed = "drm_vblank_init";
	}

	/*
	 * P3.2 intel_bios_init: the next display noirq child (VBT parse), then
	 * intel_vga_register / intel_power_domains_init(+_hw) / intel_pmdemand_init_early
	 * / intel_dmc_init / workqueues / mode config / CDCLK / color / DBUF /
	 * intel_bw_init / pmdemand / quirks / FBC.
	 */
	{
		/*
		 * intel_bios_init: real VBT acquisition (OpRegion carried from P2,
		 * else the PCI ROM read for real) or genuine-absence defaults.  It is
		 * void in the reference -- it never fails the probe.
		 */
		(void)parity_intel_bios_init_ex(&vbt_state, &pci, opregion_vbt_present, PARITY_VBT_EXPLICIT, &trace);
		/* E-123: the OpRegion service on the real OpRegion (test builds only; before N0 and any display write) */
		if (PARITY_OPREGION_FW_TEST)
			(void)parity_opregion_fw_test(osdep_pci_read32(&pci, 0xFCu), &vbt_state);
		kern_logf("i915: parity P3 intel_bios_init done: source=%d vbt_found=%d "
			"version=%u child_devices=%u\n", vbt_state.source, vbt_state.vbt_found,
			(unsigned)vbt_state.version, vbt_state.num_display_devices);
		res.last_completed = "intel_bios_init";
	}

	/*
	 * P3.3 intel_vga_register: register the VGA arbiter client (decode callback
	 * drives GMCH_CTRL on the host bridge).  A genuine -ENODEV (secondary
	 * controller) is tolerated inside the call; other errors fail the probe.
	 */
	{
		rc = parity_intel_vga_register(&vga_client, device->pci, display_ver, &trace);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "intel_vga_register"; goto teardown; }
		vga_registered = 1;
		res.last_completed = "intel_vga_register";
	}

	/*
	 * P3.4 intel_power_domains_init: sanitize options, allowed_dc_mask,
	 * target_dc_state, lock, async-put work, and build the xelpd power-well map
	 * (domain -> wells).  No power-well MMIO here (that is intel_power_domains_init_hw).
	 */
	{
		rc = parity_intel_power_domains_init(&power_domains, display_ver,
			-1 /* enable_dc auto */, -1 /* disable_power_well default */, &trace);
		if (rc != 0) { res.outcome = PARITY_FAILED; res.error = rc; res.where = "intel_power_domains_init"; goto teardown; }
		power_domains_inited = 1;
		res.last_completed = "intel_power_domains_init";
	}

	/*
	 * P3.5 intel_pmdemand_init_early: pmdemand lock + wait queue (initialised
	 * here regardless of display version, per the reference order).
	 */
	parity_intel_pmdemand_init_early(&pmdemand);
	osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_ACQUIRE, "intel_pmdemand_init_early", 0u, 0u);
	res.last_completed = "intel_pmdemand_init_early";

	/*
	 * N0 (E-120): before the first display write, record what the firmware left and decide whether the prepared start
	 * path applies (native_precheck.h).  Read-only.  STOP = the probe ends here, nothing on the display was touched.
	 */
	{
		static struct parity_native_report n0;
		struct parity_native_deps nd;

		memset(&nd, 0, sizeof(nd));
		nd.mmio = &mmio;
		nd.asls = osdep_pci_read32(&pci, 0xFCu);
		nd.gmadr_base = n0_gmadr_base;
		nd.gmadr_size = n0_gmadr_size;
		nd.ggtt_pages = ggtt_entries;
		nd.driver_ggtt_first = ggtt_entries > PARITY_GT_GGTT_PAGES + PARITY_GT_DISPLAY_PAGES ?
			ggtt_entries - PARITY_GT_GGTT_PAGES - PARITY_GT_DISPLAY_PAGES : 0u;
		nd.pipe_powered = n0_pipe_powered;
		nd.ctx = &power_domains;
		nd.vbt_pin = parity_vbt_explicit_pin();
		/* what intel_bios_init (P3) actually handed to the parser; only the explicit blob's bytes are hashed today */
		nd.parser_src = vbt_state.source;
		nd.parser_size = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_size : 0u;
		nd.parser_sha256 = vbt_state.source == PARITY_VBT_SRC_EXPLICIT_BLOB ? vbt_state.blob_sha256 : 0;
		if (vbt_state.source == PARITY_VBT_SRC_OPREGION && p2_opd != 0 && p2_opd->vbt_valid) {
			nd.parser_size = p2_opd->vbt_size;          /* the copy P2 made is what the parser consumed */
			nd.parser_sha256 = p2_opd->vbt_sha256;
		}
		n0_mmio = &mmio;
		(void)parity_native_precheck(&nd, &n0);
		if (PARITY_N0_FORCE_STOP) {
			n0.proceed = 0;
			n0.reason = "forced by the test build (PARITY_N0_FORCE_STOP): the early teardown is exercised";
		}
		parity_native_log(&n0);
		kern_logf("i915: parity N0 before-N0: PCI COMMAND firmware=0x%04x now=0x%04x (BME/MEM set at P2; restored on teardown) | done already: GT reset (GDRST full, display untouched on ADL-P), GT fault/error-register clears, fence clears (0..31), PCODE reads, MSI enabled (no handler), WC CPU view of the aperture | not done: any display register, any GGTT PTE, any D-state change\n",
			(unsigned)pci.saved_command, (unsigned)osdep_pci_read16(&pci, 0x04u));
		if (!n0.proceed) {
			res.outcome = PARITY_BLOCKED;
			res.where = "native-precheck (before any display write)";
			goto teardown;
		}
	}

	/*
	 * P3.6 intel_power_domains_init_hw(i915, false): display-core HW bring-up on
	 * the REAL device (icl_display_core_init) -- DC-state disable, PCH reset
	 * handshake, combo PHY, PW1 (fuses), CDCLK, DBUF, BW_BUDDY, xe_lpd WAs, then
	 * hold POWER_DOMAIN_INIT and sync every well.  The children share this one
	 * device state (mmio / power_domains / cdclk / sb_lock); no test overrides.
	 */
	{
		unsigned di;
		/*
		 * The cached PCI-header accessors (drv_pci_device_*) read back all-ones
		 * on this passthrough device; the live config path is the one P0 proves
		 * works (cfg_vendor=0x8086).  Read the stepping from config space.
		 */
		uint8_t revid = osdep_pci_read8(&pci, 0x08u);

		for (di = 0u; di < sizeof(cdclk); di++) ((char *)&cdclk)[di] = 0;
		parity_intel_init_cdclk_hooks(&cdclk, (int)display_ver,
			parity_adlp_display_step(revid), is_alderlake_p);
		cdclk.m = &mmio; cdclk.sb_lock = &sb_lock;

		for (di = 0u; di < sizeof(pwc); di++) ((char *)&pwc)[di] = 0;
		pwc.mmio = &mmio; pwc.vga = &vga_client; pwc.irqs_enabled = 0;

		for (di = 0u; di < sizeof(dcore); di++) ((char *)&dcore)[di] = 0;
		dcore.pd = &power_domains; dcore.cd = &cdclk; dcore.pwc = &pwc;
		dcore.m = &mmio; dcore.sb_lock = &sb_lock;
		/* the DBUF slices this display has (E-126): XE_LPD S1..S4, Tiger Lake S1..S2 */
		dcore.dbuf_slice_mask = display_ver >= 13u ? 0x0fu : 0x03u;
		/* .abox_mask of the device info: XE_LPD GENMASK(1,0), XE_D (Tiger Lake) GENMASK(2,1) */
		dcore.abox_mask = display_ver >= 13u ? 0x03u : 0x06u;
		dcore.display_ver = (int)display_ver;
		/* the platform itself, for the steps the reference gates on it (E-126) */
		dcore.is_alderlake_p = is_alderlake_p ? 1u : 0u;
		parity_lcd_set_display_ver((int)display_ver);   /* the LCD path picks its tables from this */
		dcore.dram_type = dram_info.type;
		dcore.dram_channels = dram_info.num_channels;

		parity_intel_power_domains_init_hw(&dcore, 0 /* resume=false */);
		display_core_inited = dcore.reached_init_ref;

		if (dcore.fault_stop) {
			res.outcome = PARITY_FAILED;
			res.error = -EIO;
			res.where = (dcore.fault_where != 0) ? dcore.fault_where
							     : "intel_power_domains_init_hw";
			kern_logf("i915: parity P3 intel_power_domains_init_hw(false): "
				"adaptation-layer fault at %s\n", res.where);
			goto teardown;
		}
		kern_logf("i915: parity P3 intel_power_domains_init_hw(false) done: "
			"cdclk=%u vco=%u dbuf=0x%x init_ref=%d sync_hw=%d\n",
			cdclk.hw.cdclk, cdclk.hw.vco, dcore.dbuf_enabled_slices,
			dcore.init_wakeref_held, dcore.reached_sync_hw);
		res.last_completed = "intel_power_domains_init_hw";
	}

	/*
	 * P3.7 intel_dmc_init: acquire + parse + load the DMC firmware ASYNCHRONOUSLY.
	 * A DMC-owned POWER_DOMAIN_INIT reference is taken (released by the worker on a
	 * successful load); the work is queued and probe continues WITHOUT waiting for
	 * the load to finish (no completion barrier on the normal path).
	 */
	if (parity_kworkqueue_create(&dmc_wq, "i915-dmc") == 0) {
		unsigned di;
		/*
		 * The cached PCI-header accessors (drv_pci_device_*) read back all-ones
		 * on this passthrough device; the live config path is the one P0 proves
		 * works (cfg_vendor=0x8086).  Read the stepping from config space.
		 */
		uint8_t drev = osdep_pci_read8(&pci, 0x08u);
		int dstep = parity_adlp_display_step(drev);
		char sc = (char)('A' + (dstep - PARITY_STEP_A0) / 4);
		char ss = (char)('0' + (dstep - PARITY_STEP_A0) % 4);

		for (di = 0u; di < sizeof(dmc_dev); di++) ((char *)&dmc_dev)[di] = 0;
		parity_intel_dmc_init(&dmc_dev, &dmc_wq, &mmio, &power_domains, &pwc,
			(int)display_ver, sc, ss,
			/* E-126: the DMC of THIS display (intel_dmc.c: TGL_DMC_PATH / ADLP_DMC_PATH) */
			display_ver >= 13u ? "i915/adlp_dmc.bin" : "i915/tgl_dmc_ver2_12.bin");
		dmc_inited = 1;
		pwc.display_ver = (int)display_ver;
		kern_logf("i915: parity P3 intel_dmc_init: revid=0x%x step=%c%c DMC load queued (path=%s, work_submitted=%d)\n",
			(unsigned)drev, sc, ss, dmc_dev.fw_path, dmc_dev.work_submitted);
		res.last_completed = "intel_dmc_init";
	}

	/*
	 * P3.8 modeset/flip workqueues (intel_modeset_wq / intel_flip_wq): created
	 * here as the reference does before mode-config init.
	 */
	modeset_wq_ok = (parity_kworkqueue_create(&modeset_wq, "i915-modeset") == 0);
	flip_wq_ok = (parity_kworkqueue_create(&flip_wq, "i915-flip") == 0);
	kern_logf("i915: parity P3 modeset/flip workqueues: modeset=%d flip=%d\n",
		modeset_wq_ok, flip_wq_ok);
	res.last_completed = "intel_modeset_flip_wq";

	/*
	 * P3.9..P3.16 -- the tail of intel_display_driver_probe_noirq():
	 *
	 *   intel_mode_config_init  (void)
	 *   intel_cdclk_init        -> global state object
	 *   intel_color_init        -> DISPLAY_VER == 10 only (ADL-P: early 0)
	 *   intel_dbuf_init         -> global state object
	 *   intel_bw_init           -> global state object + forced SAGV disable (PCODE)
	 *   intel_pmdemand_init     -> global state object
	 *   intel_init_quirks       (void)
	 *   intel_fbc_init          (void)
	 *
	 * The five that return int share the reference's single error label, which
	 * unwinds via intel_dmc_fini + intel_power_domains_driver_remove -- exactly
	 * the teardown order below, so a failure just jumps there.
	 */
	{
		unsigned dsi;
		int rc3;
		uint16_t q_dev, q_svid, q_sdid;

		for (dsi = 0u; dsi < sizeof(dstate); dsi++) ((char *)&dstate)[dsi] = 0;
		dstate.enable_fbc_param = -1;   /* display.params.enable_fbc default */

		parity_intel_mode_config_init(&dstate, (int)display_ver, PARITY_PLAT_NONE);
		res.last_completed = "intel_mode_config_init";
		dstate_inited = 1;

		rc3 = parity_intel_cdclk_init(&dstate);
		if (rc3 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc3;
			res.where = "intel_cdclk_init";
			goto teardown;
		}
		res.last_completed = "intel_cdclk_init";

		rc3 = parity_intel_color_init(&dstate, (int)display_ver);
		if (rc3 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc3;
			res.where = "intel_color_init";
			goto teardown;
		}
		res.last_completed = "intel_color_init";

		rc3 = parity_intel_dbuf_init(&dstate);
		if (rc3 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc3;
			res.where = "intel_dbuf_init";
			goto teardown;
		}
		res.last_completed = "intel_dbuf_init";

		rc3 = parity_intel_bw_init(&dstate, (int)display_ver, &bw_state,
			&sb_lock, &mmio);
		if (rc3 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc3;
			res.where = "intel_bw_init";
			goto teardown;
		}
		kern_logf("i915: parity P3 intel_bw_init: obj=%u sagv_forced=%d qgv=0x%x psf=0x%x "
			"mask=0x%x pcode=%d sagv_status=%d\n",
			dstate.obj_count, dstate.sagv_force_disable_attempted,
			dstate.sagv_qgv_points, dstate.sagv_psf_points,
			(unsigned)dstate.bw_obj_state.qgv_points_mask,
			dstate.sagv_pcode_ret, bw_state.sagv_status);
		res.last_completed = "intel_bw_init";

		rc3 = parity_intel_pmdemand_init(&dstate, (int)display_ver, &mmio, -1);
		if (rc3 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc3;
			res.where = "intel_pmdemand_init";
			goto teardown;
		}
		res.last_completed = "intel_pmdemand_init";

		/* Live config space, for the same reason as the stepping reads above. */
		q_dev = osdep_pci_read16(&pci, 0x02u);
		q_svid = osdep_pci_read16(&pci, 0x2cu);
		q_sdid = osdep_pci_read16(&pci, 0x2eu);
		/*
		 * Diagnostic only (no behaviour change): compare the cached PCI-header
		 * accessors with a LIVE config read of the same fields.  P0 proves the
		 * live path works on this device (cfg_vendor=0x8086).
		 */
		kern_logf("i915: parity PCIID cached: ven=0x%04x dev=0x%04x rev=0x%02x subsys=%04x:%04x | "
			"live: ven=0x%04x dev=0x%04x rev=0x%02x subsys=%04x:%04x\n",
			(unsigned)drv_pci_device_vendor(device->pci),
			(unsigned)drv_pci_device_product(device->pci),
			(unsigned)drv_pci_device_revision(device->pci),
			(unsigned)drv_pci_device_subvendor(device->pci),
			(unsigned)drv_pci_device_subproduct(device->pci),
			(unsigned)osdep_pci_read16(&pci, 0x00u), (unsigned)osdep_pci_read16(&pci, 0x02u),
			(unsigned)osdep_pci_read8(&pci, 0x08u),
			(unsigned)osdep_pci_read16(&pci, 0x2cu), (unsigned)osdep_pci_read16(&pci, 0x2eu));
		parity_intel_init_quirks(&dstate, q_dev, q_svid, q_sdid);
		kern_logf("i915: parity P3 intel_init_quirks: dev=0x%x subsys=%04x:%04x "
			"quirk_mask=0x%x hooks=%u dmi_available=%d\n",
			(unsigned)q_dev, (unsigned)q_svid, (unsigned)q_sdid,
			dstate.quirk_mask, dstate.quirk_hooks_fired, dstate.dmi_available);
		res.last_completed = "intel_init_quirks";

		/* ADL-P (xe_lpd) runtime fbc_mask = BIT(INTEL_FBC_A). */
		parity_intel_fbc_init(&dstate, (int)display_ver, 0x1u, 0 /* vtd */,
			PARITY_PLAT_NONE);
		kern_logf("i915: parity P3 intel_fbc_init: fbc_mask=0x%x enable_fbc=%d "
			"created=%u funcs=%d\n",
			dstate.fbc_mask, dstate.enable_fbc_sanitized, dstate.fbc_created,
			dstate.fbc[0] != 0 ? dstate.fbc[0]->funcs_kind : -1);
		res.last_completed = "intel_fbc_init";

		kern_logf("i915: parity P3 probe_noirq COMPLETE: global objs=%u "
			"(order: %s,%s,%s,%s) mode_config max=%ux%u cursor=%ux%u async_flip=%d\n",
			dstate.obj_count,
			dstate.cdclk_obj.name, dstate.dbuf_obj.name,
			dstate.bw_obj.name, dstate.pmdemand_obj.name,
			dstate.mode_config.max_width, dstate.mode_config.max_height,
			dstate.mode_config.cursor_width, dstate.mode_config.cursor_height,
			dstate.mode_config.async_page_flip);
	}

	/* intel_display_driver_probe_noirq() returns 0 here.  P4 follows. */
	res.last_completed = "intel_display_driver_probe_noirq";

	/*
	 * ============================== P4 ==============================
	 * intel_irq_install():  irqs_enabled/irq_enabled -> intel_irq_reset ->
	 * request_irq -> intel_irq_postinstall.  The MSI vector was allocated in
	 * P2 with NO handler attached, so request_irq maps onto the HAL's attach
	 * half (hal_irq_attach_msi) with no HAL change.
	 *
	 * The PCH type is needed first: gen11_display_irq_reset() resets SDE and
	 * gen8_de_irq_postinstall() runs icp_irq_postinstall() only for
	 * INTEL_PCH_TYPE >= PCH_ICP.
	 */
	if (!msi_kept || !osdep_pci_msi_enabled(&pci)) {
		kern_logf("i915: parity P4 intel_irq_install: no MSI vector retained "
			"(msi_kept=%d) -- cannot attach a handler\n", msi_kept);
		res.outcome = PARITY_FAILED;
		res.error = -ENODEV;
		res.where = "intel_irq_install";
		goto teardown;
	}
	{
		unsigned ii;
		int rc4;

		for (ii = 0u; ii < sizeof(pch); ii++) ((char *)&pch)[ii] = 0;
		parity_intel_detect_pch(&pch, (int)display_ver, 1 /* ADL-P */,
			1 /* HAS_DISPLAY */, 1 /* running as a guest */);
		kern_logf("i915: parity P4 intel_detect_pch: type=%d id=0x%04x source=%d "
			"bridges=%u bridge_dev=0x%04x subsys=%04x:%04x\n",
			pch.type, (unsigned)pch.id, pch.source, pch.bridges_scanned,
			(unsigned)pch.bridge_device, (unsigned)pch.bridge_svid,
			(unsigned)pch.bridge_sdid);
		res.last_completed = "intel_detect_pch";

		for (ii = 0u; ii < sizeof(irqdev); ii++) ((char *)&irqdev)[ii] = 0;
		irqdev.m = &mmio;
		irqdev.pd = &power_domains;
		irqdev.pwc = &pwc;
		/* the pipes' interrupt control + vblank delivery, and the power-well hooks that restore / stop it */
		parity_irq_vblank_init(&irqdev, &irq_vblank);
		pwc.irq_ops = &parity_pw_irq_ops;
		pwc.irq_ctx = &irqdev;
		irqdev.pch = &pch;
		irqdev.display_ver = (int)display_ver;
		irqdev.pipe_mask = 0xfu;              /* ADL-P (xe_lpd) A|B|C|D */
		irqdev.cpu_transcoder_mask = 0xfu;    /* A|B|C|D (DSI transcoders separate) */
		irqdev.has_display = 1;
		irqdev.submission = PARITY_SUBMISSION_EXECLISTS;
		irqdev.gt = &gtmmio;   /* GT irq identity -> engine lookup */
		irqdev.dsi_present = 0;               /* VBT reported no DSI child device */
		irqdev.msi_irq = pci_priv.msi_irq;

		rc4 = parity_intel_irq_install(&irqdev);
		kern_logf("i915: parity P4 intel_irq_install: rc=%d msi_irq=%d attached=%d "
			"gt_irqs=0x%x dmask=0x%x smask=0x%x reset_writes=%u post_writes=%u\n",
			rc4, irqdev.msi_irq, irqdev.handler_attached,
			irqdev.gt_irqs, irqdev.gt_dmask, irqdev.gt_smask,
			irqdev.reset_writes, irqdev.postinstall_writes);
		kern_logf("i915: parity P4 de masks: pipe_masked=0x%x pipe_enables=0x%x "
			"port_masked=0x%x misc_masked=0x%x de_irq_mask[A]=0x%x master_enabled=%d\n",
			irqdev.de_pipe_masked, irqdev.de_pipe_enables, irqdev.de_port_masked,
			irqdev.de_misc_masked, irqdev.de_irq_mask[0], irqdev.reached_master_enable);
		if (rc4 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc4;
			res.where = "intel_irq_install";
			goto teardown;
		}
		irq_installed = 1;
		res.last_completed = "intel_irq_install";

		/*
		 * Give the device a brief window to deliver anything it has pending,
		 * purely to observe that the vector reaches our handler.  The GT and
		 * display bottom halves belong to P5/P6, so nothing is serviced yet.
		 */
		kern_usleep_range(20000u, 30000u);
		kern_logf("i915: parity P4 irq observed: count=%u handled=%u none=%u "
			"gt=%u display=%u last_master_ctl=0x%x gu_misc_iir=0x%x\n",
			irqdev.irq_count, irqdev.irq_handled_count, irqdev.irq_none_count,
			irqdev.gt_irq_count, irqdev.display_irq_count,
			irqdev.last_master_ctl, irqdev.last_gu_misc_iir);
		kern_logf("i915: parity P4 gt irq: banks=%u/%u identity=%u invalid=%u "
			"engine=%u other=%u unknown=%u user=%u ctxsw=%u err=%u sema=%u\n",
			irqdev.gt_bank_acks[0], irqdev.gt_bank_acks[1],
			irqdev.gt_identity_reads, irqdev.gt_identity_invalid,
			irqdev.gt_engine_intrs, irqdev.gt_other_intrs,
			irqdev.gt_unknown_class, irqdev.gt_user_intr,
			irqdev.gt_ctx_switch_intr, irqdev.gt_error_intr,
			irqdev.gt_semaphore_intr);
	}

	/*
	 * P5-0 SURVEY (diagnostic only, no state change).  Before implementing the
	 * P5 readout+sanitize we need to know what the BIOS / pre-OS actually left
	 * enabled on THIS device, because that decides whether
	 * intel_sanitize_crtc() must drive the full intel_crtc_disable_noatomic()
	 * path (a modeset disable) or can early-return on every pipe.
	 *
	 * Pipe/transcoder/plane registers live behind pipe power wells, so each
	 * read is gated exactly as the reference readout gates it; an ungated read
	 * would return garbage and be worse than no data.
	 */
	{
		unsigned pi;
		static const unsigned ddi_ports[6] = { 0u, 1u, 3u, 4u, 5u, 6u };
		static const char *ddi_names[6] = { "A", "B", "TC1", "TC2", "TC3", "TC4" };
		static const uint32_t pll_regs[7] = {
			0x46010u, 0x46014u,            /* DPLL0, DPLL1 */
			0x46020u,                      /* TBT PLL */
			0x46030u, 0x46034u, 0x46038u, 0x4603cu   /* TC PLL 1..4 */
		};
		static const char *pll_names[7] = {
			"DPLL0", "DPLL1", "TBT", "TC1", "TC2", "TC3", "TC4"
		};
		unsigned wells_on = 0u, wells_on_unused = 0u;

		for (pi = 0u; pi < 4u; pi++) {
			int powered = parity_display_power_is_enabled(&power_domains,
				(enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pi), &pwc);
			int tpowered = parity_display_power_is_enabled(&power_domains,
				(enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + pi), &pwc);

			if (!powered && !tpowered) {
				kern_logf("i915: parity P5-0 survey pipe %c: power OFF "
					"(pipe=%d trans=%d) -- not read\n",
					(char)('A' + pi), powered, tpowered);
				continue;
			}
			kern_logf("i915: parity P5-0 survey pipe %c: power(pipe=%d trans=%d) "
				"TRANSCONF=0x%08x TRANS_DDI_FUNC_CTL=0x%08x PLANE_CTL(1)=0x%08x\n",
				(char)('A' + pi), powered, tpowered,
				osdep_mmio_read32(&mmio, 0x70008u + pi * 0x1000u),
				osdep_mmio_read32(&mmio, 0x60400u + pi * 0x1000u),
				osdep_mmio_read32(&mmio, 0x70180u + pi * 0x1000u));
		}

		for (pi = 0u; pi < 6u; pi++)
			kern_logf("i915: parity P5-0 survey DDI %s: DDI_BUF_CTL=0x%08x\n",
				ddi_names[pi],
				osdep_mmio_read32(&mmio, 0x64000u + ddi_ports[pi] * 0x100u));

		for (pi = 0u; pi < 7u; pi++)
			kern_logf("i915: parity P5-0 survey PLL %s: ENABLE=0x%08x\n",
				pll_names[pi], osdep_mmio_read32(&mmio, pll_regs[pi]));

		for (pi = 0u; pi < power_domains.num_power_wells; pi++) {
			struct parity_power_well *w = &power_domains.power_wells[pi];

			if (w->hw_enabled != 1)
				continue;
			wells_on++;
			if (!w->always_on && w->refcount == 0u) {
				wells_on_unused++;
				kern_logf("i915: parity P5-0 survey well '%s': ON but refcount=0 "
					"(a sanitize candidate)\n", w->name);
			}
		}
		kern_logf("i915: parity P5-0 survey wells: total=%u on=%u on_unused=%u\n",
			power_domains.num_power_wells, wells_on, wells_on_unused);
		res.last_completed = "P5-0 readout survey";
	}

	/*
	 * ============================== P5 ==============================
	 * intel_display_driver_probe_nogem().  P5-a is the front section: wm/SAGV
	 * latency, PPS + GMBUS bases, the per-pipe CRTC and plane records, the
	 * shared DPLL table, cdclk init_hw + the ADL-P display WAs, and the VGA
	 * plane disable.  intel_setup_outputs() (P5-b) and the readout/sanitize
	 * (P5-c / P5-d) follow.
	 */
	{
		int rc5 = parity_intel_display_nogem_front(&nogem, (int)display_ver,
			0xfu /* ADL-P pipe_mask */, &mmio, &sb_lock, &cdclk, &bw_state,
			&vga_client);

		if (rc5 != 0) {
			res.outcome = PARITY_FAILED;
			res.error = rc5;
			res.where = nogem.fail_where != 0 ? nogem.fail_where
							 : "intel_display_driver_probe_nogem";
			goto teardown;
		}
		nogem_inited = 1;
		kern_logf("i915: parity P5a wm: levels=%u latency=%u/%u/%u/%u/%u/%u/%u/%u "
			"valid=%d sagv_status=%d block_time=%uus\n",
			nogem.wm_num_levels,
			nogem.wm_skl_latency[0], nogem.wm_skl_latency[1],
			nogem.wm_skl_latency[2], nogem.wm_skl_latency[3],
			nogem.wm_skl_latency[4], nogem.wm_skl_latency[5],
			nogem.wm_skl_latency[6], nogem.wm_skl_latency[7],
			nogem.wm_latency_valid, nogem.sagv_status,
			nogem.sagv_block_time_us);
		kern_logf("i915: parity P5a objects: crtcs=%u planes/crtc=%u scalers=%u "
			"dplls=%u(mgr=%d) gmbus_pins=%u pps_base=0x%x gmbus_base=0x%x\n",
			nogem.num_crtcs, nogem.crtcs[0].num_planes,
			nogem.crtcs[0].num_scalers, nogem.num_dplls,
			nogem.dpll_mgr_present, nogem.gmbus_pins_present,
			nogem.pps_mmio_base, nogem.gmbus_mmio_base);
		kern_logf("i915: parity P5a hw: max_cdclk=%u nssc_ref=%u adlp_wa=%d "
			"hti_read=%d vga_already_off=%d vga_disabled=%d writes=%u "
			"(gmbus_adapters/hdcp_component unimplemented: %d/%d)\n",
			nogem.max_cdclk_freq, nogem.dpll_ref_nssc, nogem.adlp_wa_applied,
			nogem.hti_state_read, nogem.vga_already_disabled,
			nogem.vga_disable_done, nogem.mmio_writes,
			nogem.gmbus_adapters_unimplemented, nogem.hdcp_component_unimplemented);
		res.last_completed = "P5a display_nogem_front";
	}

	/*
	 * P5-b intel_setup_outputs(): HAS_DDI -> intel_ddi_crt_present() (false on
	 * ver >= 9) -> intel_bios_for_each_encoder(intel_ddi_init).  The DDI
	 * decision chain runs in full; the DRM encoder registration and the
	 * DP/HDMI/AUX/HPD/connector construction are out of the approved scope.
	 */
	{
		unsigned ei;

		/* the eDP connector is built HERE, inside intel_ddi_init, as the reference does */
		parity_edp_device_prepare(&edp_dev, &mmio, &power_domains, &pwc, &vbt_state);
		nogem.dp_connector_init = parity_edp_device_init_connector;
		nogem.dp_connector_ctx = &edp_dev;
		parity_intel_setup_outputs(&nogem, (int)display_ver,
			(1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) |
			(1u << 5) | (1u << 6),   /* ADL-P port_mask: A,B,TC1..TC4 */
			&vbt_state, &mmio);
		kern_logf("i915: parity P5b setup_outputs: vbt_children=%u ddi_init=%u "
			"encoders=%u skipped=%u crt_present=%d\n",
			vbt_state.num_display_devices, nogem.ddi_init_calls,
			nogem.num_encoders, nogem.ddi_skipped, nogem.crt_present);
		for (ei = 0u; ei < nogem.num_encoders; ei++)
			kern_logf("i915: parity P5b encoder[%u]: port=%c phy=%d tc=%d "
				"clk=%d pd=%d dvo=0x%02x dev_type=0x%x dp=%d hdmi=%d\n",
				ei, (char)('A' + nogem.encoders[ei].port),
				nogem.encoders[ei].phy, nogem.encoders[ei].is_tc,
				nogem.encoders[ei].clk_funcs, nogem.encoders[ei].power_domain,
				(unsigned)nogem.encoders[ei].dvo_port,
				nogem.encoders[ei].device_type,
				nogem.encoders[ei].init_dp, nogem.encoders[ei].init_hdmi);
		for (ei = 0u; ei < nogem.num_ddi_skips; ei++)
			kern_logf("i915: parity P5b ddi skip[%u]: port=%d reason=%d\n",
				ei, nogem.ddi_skip_port[ei], nogem.ddi_skip_reason[ei]);
		res.last_completed = "intel_setup_outputs";
	}

	/*
	 * P5-c intel_modeset_setup_hw_state(), readout half.  The reference takes a
	 * POWER_DOMAIN_INIT reference around the whole of setup_hw_state; the P3
	 * init reference is still held here, so the domain is already powered.
	 * intel_early_display_was() is IS_DISPLAY_VER(10,12) only -- not ADL-P.
	 */
	{
		parity_intel_modeset_readout_hw_state(&nogem, (int)display_ver, &mmio,
			&power_domains, &pwc);
		kern_logf("i915: parity P5c readout: crtcs=%u active_pipes=0x%x "
			"planes_visible=%u encoders_linked=%u dplls_on=%u "
			"(detail readout unimplemented: %d)\n",
			nogem.readout_crtcs, nogem.active_pipes,
			nogem.readout_planes_visible, nogem.readout_encoders_linked,
			nogem.readout_dplls_on, nogem.readout_detail_unimplemented);
		res.last_completed = "intel_modeset_readout_hw_state";
	}

	/*
	 * P5-d intel_modeset_setup_hw_state(), sanitize half.  Turns back off what
	 * the pre-OS left in an unused or inconsistent state: unused shared DPLLs,
	 * unused power wells, an active FBC, an ungated DDI clock on a disabled
	 * encoder.  On a quiescent device every one of these is a no-op.
	 */
	{
		uint8_t srev = osdep_pci_read8(&pci, 0x08u);

		parity_intel_modeset_sanitize_hw_state(&nogem, (int)display_ver,
			parity_adlp_display_step(srev), dstate.fbc_mask, &mmio,
			&power_domains, &pwc);
		kern_logf("i915: parity P5d sanitize: vblank_resets=%u dmc_pipes=%u "
			"vblank_on=%u fbc_deact=%u enc_clk_gated=%u dplls_disabled=%u "
			"wells_disabled=%u cmtg_wa=%d early_was=%d wm_read=%d "
			"(crtc_disable_noatomic needed: %d)\n",
			nogem.vblank_resets, nogem.dmc_pipes_enabled,
			nogem.vblank_on_count, nogem.fbc_deactivated,
			nogem.encoder_clocks_gated, nogem.dplls_disabled,
			nogem.wells_disabled, nogem.cmtg_wa_applied,
			nogem.early_display_was_applied, nogem.wm_hw_state_read,
			nogem.crtc_disable_noatomic_unimplemented);
		res.last_completed = "intel_modeset_setup_hw_state";
	}

	/* intel_display_driver_probe_nogem() returns 0 here. */
	res.last_completed = "intel_display_driver_probe_nogem";

	/*
	 * ============================== P6 ==============================
	 * i915_gem_init() -> intel_gt_init().  P6-a builds every table (GT /
	 * engine / context workarounds, the whitelist, MOCS, RC6, RPS); P6-b
	 * programs the hardware.  The reference wraps the whole of intel_gt_init
	 * and intel_gt_init_hw in FORCEWAKE_ALL ("double layer security
	 * blanket"), so the per-register forcewake ranges are not needed here.
	 *
	 * intel_clock_gating_init() is a no-op on ADL-P (no hook matches), and
	 * intel_uc_* are all no-ops with enable_guc=0 (uc_ops_off).
	 */

	{
		unsigned ei;

		/*
		 * intel_gt_init() holds intel_uncore_forcewake_get(FORCEWAKE_ALL)
		 * across its whole body.  On Gen12 that is not just RENDER+GT: the
		 * engine registers of VCS0/VCS2/VECS0 live in their own media
		 * domains, and the ADL-P workaround list writes two of them
		 * (Wa_14011060649 at 0x1c3f10 and 0x1d3f10).  Taking only RENDER+GT
		 * would leave those accesses uncovered.
		 */
		rc = osdep_fw_get(&mmio, OSDEP_FW_RENDER);
		if (rc == 0)
			rc = osdep_fw_get(&mmio, OSDEP_FW_GT);
		if (rc == 0)
			rc = osdep_fw_get(&mmio, OSDEP_FW_MEDIA_VDBOX0);
		if (rc == 0)
			rc = osdep_fw_get(&mmio, OSDEP_FW_MEDIA_VDBOX2);
		if (rc == 0)
			rc = osdep_fw_get(&mmio, OSDEP_FW_MEDIA_VEBOX0);
		if (rc != 0) {
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = "forcewake_gt_init"; goto teardown;
		}

		for (ei = 0u; ei < sizeof(gtinit); ei++) ((char *)&gtinit)[ei] = 0;
		parity_gt_init_tables(&gtinit, &gtmmio, 12, &sb_lock, &mmio);

		kern_logf("i915: parity P6a tables: gt_wa=%u mocs(uc_index=%u "
			"entries=%u) rps(rp0=%u rp1=%u min=%u eff=%u pcode=%d) "
			"rc6_supported=%d\n",
			gtinit.gt_wa.count, gtinit.mocs.uc_index, gtinit.mocs.n_entries,
			gtinit.rps.rp0_freq, gtinit.rps.rp1_freq, gtinit.rps.min_freq,
			gtinit.rps.efficient_freq, gtinit.rps.pcode_ok,
			gtinit.rc6.supported);
		parity_wa_list_dump(&gtinit.gt_wa, "GT");
		for (ei = 0u; ei < gtmmio.num_engines && ei < 6u; ei++) {
			kern_logf("i915: parity P6a %s: engine_wa=%u ctx_wa=%u "
				"whitelist=%u\n", gtmmio.engines[ei].name,
				gtinit.engine_wa[ei].count, gtinit.ctx_wa[ei].count,
				gtinit.whitelist[ei].count);
			if (gtmmio.engines[ei].class == PARITY_RENDER_CLASS) {
				parity_wa_list_dump(&gtinit.engine_wa[ei], "ENGINE");
				parity_wa_list_dump(&gtinit.ctx_wa[ei], "CTX");
				parity_wa_list_dump(&gtinit.whitelist[ei], "WHITELIST");
			}
		}
		res.last_completed = "intel_gt_init (tables)";

		/*
		 * intel_gt_init_scratch(gt, SZ_4K) and kernel_vm(gt): the GT's own
		 * objects live in a window at the top of the GGTT P2 mapped, and
		 * the kernel context gets its own 4-level ppgtt.
		 */
		rc = parity_gt_mem_init(&gtmem, dma39, (((uint64_t)1) << 39) - 1u,
			device->gtt.address, ggtt_entries, scratch_pte, &mmio);
		if (rc == 0) {
			gtmem_inited = 1;
			rc = parity_gt_init_scratch(&gtmem, &gt_scratch);
		}
		if (rc == 0)
			rc = parity_gt_ppgtt_create(&gtmem, &gtpp);
		if (rc != 0) {
			kern_logf("i915: parity P6c GT memory setup failed rc=%d\n", rc);
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = "intel_gt_init_scratch/kernel_vm";
			goto p6_fw_out;
		}
		kern_logf("i915: parity P6c mem: ggtt window first=%u pages=%u (of %u) "
			"scratch ggtt=0x%llx ppgtt top_pd dma=0x%llx objects=%u\n",
			gtmem.window_first, gtmem.window_pages, ggtt_entries,
			(unsigned long long)gt_scratch->ggtt_offset,
			(unsigned long long)gtpp.top_pd_dma, gtmem.objects_live);

		/* intel_engines_init(): status pages, execlists state, kernel contexts. */
		rc = parity_intel_engines_init(&gteng, &gtmmio, &gtmem, &gtpp);
		if (rc != 0) {
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = "intel_engines_init";
			goto p6_fw_out;
		}
		gteng_inited = 1;
		for (ei = 0u; ei < gteng.n; ei++)
			kern_logf("i915: parity P6c %s: hwsp=0x%llx kctx state=0x%llx "
				"(%u pages, wa_bb_page=%u) ring=0x%llx lrca=0x%08x ccid=0x%08x "
				"bb=%u dw\n", gteng.ge[ei].info->name,
				(unsigned long long)gteng.ge[ei].hwsp_ggtt,
				(unsigned long long)gteng.kernel_ce[ei].state->ggtt_offset,
				gteng.kernel_ce[ei].state->pages,
				gteng.kernel_ce[ei].wa_bb_page,
				(unsigned long long)gteng.kernel_ce[ei].ring.ggtt_offset,
				gteng.kernel_ce[ei].lrca, gteng.ge[ei].ccid,
				gteng.kernel_ce[ei].indirect_bb_dwords);
		res.last_completed = "intel_engines_init";

		/* intel_gt_resume(), in the reference order (see gt_resume.h). */
		rc = parity_intel_gt_resume(&gteng, &gtinit, &gtmmio, &mmio, &uncore_lock);
		kern_logf("i915: parity P6c resume: rc=%d reset_engines=%d stop_cs_timeouts=%u "
			"engines_resumed=%u l3cc(rcs, mocs_init_engine)=%u\n",
			rc, gteng.reset_rc, gteng.stop_cs_timeouts, gteng.resumed,
			gteng.l3cc_writes_rcs);
		kern_logf("i915: parity P6b hw: pat=%d gt_wa(w=%u skip=%u ok=%u "
			"mismatch=%u noverify=%u) mocs(global=%u l3cc=%u) "
			"whitelist_writes=%u engines=%u\n",
			gtinit.pat_programmed, gtinit.gt_wa_applied.written,
			gtinit.gt_wa_applied.skipped_unchanged,
			gtinit.gt_wa_applied.verified, gtinit.gt_wa_applied.mismatched,
			gtinit.gt_wa_applied.not_verifiable,
			gtinit.mocs_global_writes, gtinit.mocs_l3cc_writes,
			gtinit.whitelist_writes, gtinit.engines_resumed);
		for (ei = 0u; ei < gtmmio.num_engines && ei < 6u; ei++)
			kern_logf("i915: parity P6b %s wa: w=%u skip=%u ok=%u "
				"mismatch=%u noverify=%u\n", gtmmio.engines[ei].name,
				gtinit.engine_wa_applied[ei].written,
				gtinit.engine_wa_applied[ei].skipped_unchanged,
				gtinit.engine_wa_applied[ei].verified,
				gtinit.engine_wa_applied[ei].mismatched,
				gtinit.engine_wa_applied[ei].not_verifiable);
		kern_logf("i915: parity P6b pm: rps_enabled=%d rc6_enabled=%d "
			"rc6_ctl=0x%x pg_enable=0x%x (rc6 skipped by switch: %d)\n",
			gtinit.rps.enabled, gtinit.rc6.enabled, gtinit.rc6.ctl_enable,
			gtinit.rc6.pg_enable, gtinit.rc6.wa_disabled);

		/*
		 * Read back what enable_execlists() programmed.  The reference does
		 * not read these; the reads have no side effect and are the only way
		 * to see from the log that the hardware took the programming.
		 */
		for (ei = 0u; ei < gteng.n; ei++) {
			uint32_t b = gteng.ge[ei].info->mmio_base;

			kern_logf("i915: parity P6c %s resume: stop_cs=%d mi_fw_pending=0x%x "
				"esr=0x%x | readback HWS_PGA=0x%08x (want 0x%08x) MODE_GEN7=0x%08x "
				"MI_MODE=0x%08x HEAD=0x%08x TAIL=0x%08x\n",
				gteng.ge[ei].info->name, gteng.ge[ei].stop_cs_rc,
				gteng.ge[ei].mi_fw_pending, gteng.ge[ei].esr_at_resume,
				osdep_mmio_read32(&mmio, b + 0x80u),
				(uint32_t)gteng.ge[ei].hwsp_ggtt,
				osdep_mmio_read32(&mmio, b + 0x29cu),
				osdep_mmio_read32(&mmio, b + 0x9cu),
				osdep_mmio_read32(&mmio, b + 0x34u),
				osdep_mmio_read32(&mmio, b + 0x30u));
		}
		if (rc != 0) {
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = "intel_gt_resume";
			goto p6_fw_out;
		}
		res.last_completed = "intel_gt_resume";
		gteng_resumed = 1;

		/*
		 * __engines_record_defaults(): the first requests this port puts on
		 * the GPU -- ctx workarounds + breadcrumb on a fresh context per
		 * engine, then the park switch to the kernel context.  No batch, no
		 * drawing.  intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT = HZ/5).
		 */
		{
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_engine_intrs, r0 = irqdev.gt_error_intr;

			rc = parity_engines_record_defaults(&gtdef, &gteng, &gtinit, &gtmem,
				&gtpp, &mmio, &uncore_lock, 200u);
			gtdef_inited = 1;
			kern_logf("i915: parity P6c record_defaults: rc=%d where=%s polls=%u "
				"timed_out=%d wedged=%d | gt irq during: user=%u ctx_switch=%u "
				"engine=%u error=%u\n",
				rc, gtdef.err_where != 0 ? gtdef.err_where : "-", gtdef.polls,
				gtdef.timed_out, gtdef.wedged,
				irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
				irqdev.gt_engine_intrs - e0, irqdev.gt_error_intr - r0);
		}
		for (ei = 0u; ei < gteng.n; ei++) {
			struct parity_execlists *el = &gteng.el[ei];

			kern_logf("i915: parity P6c %s defaults: state=%d rq_seqno=%u krq_seqno=%u "
				"hwsp_seqno=%u | el submits=%u promotes=%u completes=%u errors=%u "
				"late=%u mmio=%u serial=%u wakeref_serial=%u\n",
				gteng.ge[ei].info->name, gtdef.state[ei], gtdef.rq[ei].seqno,
				gtdef.krq[ei].seqno,
				gteng.ge[ei].hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u],
				el->submits, el->promotes, el->completes, el->csb_errors,
				el->csb_late, el->csb_mmio_fallback, el->serial, el->wakeref_serial);
			if (gtdef.default_state[ei] != 0) {
				const uint32_t *r = (const uint32_t *)
					((const char *)gtdef.default_state[ei]->cpu + 4096u);

				/*
				 * What the engine SAVED: the LRI header it rewrote, the ring
				 * registers, the timestamp and (render) RPCS.
				 */
				kern_logf("i915: parity P6c %s default_state: %u bytes | reg[1]=%08x "
					"CTX_CTRL=%08x RING_HEAD=%08x RING_TAIL=%08x RING_START=%08x "
					"RING_CTL=%08x TIMESTAMP=%08x RPCS=%08x\n",
					gteng.ge[ei].info->name, gtdef.default_state[ei]->bytes,
					r[1], r[PARITY_CTX_CONTEXT_CONTROL], r[PARITY_CTX_RING_HEAD],
					r[PARITY_CTX_RING_TAIL], r[PARITY_CTX_RING_START],
					r[PARITY_CTX_RING_CTL], r[PARITY_CTX_TIMESTAMP],
					r[PARITY_CTX_R_PWR_CLK_STATE]);
			}
		}
		if (rc != 0) {
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = gtdef.err_where != 0 ? gtdef.err_where : "__engines_record_defaults";
			goto p6_fw_out;
		}
		res.last_completed = "__engines_record_defaults";
		/* engine->default_state: every later context starts from it. */
		for (ei = 0u; ei < gteng.n; ei++)
			gteng.ge[ei].default_state = gtdef.default_state[ei];

		/*
		 * __engines_verify_workarounds() (a permanent diagnostic here; the
		 * reference compiles it under DEBUG_GEM): per engine an SRM request
		 * on the kernel context stores every engine workaround register to
		 * memory, and the stored values are compared with the list.  The
		 * first request of this port that makes the GPU read registers.
		 */
		{
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_engine_intrs, r0 = irqdev.gt_error_intr;

			rc = parity_engines_verify_workarounds(&gtvwa, &gteng, &gtinit, &gtmem,
				&mmio, 200u);
			gtvwa_inited = 1;
			kern_logf("i915: parity P6c verify_workarounds: rc=%d where=%s polls=%u "
				"timed_out=%d | gt irq during: user=%u ctx_switch=%u engine=%u error=%u\n",
				rc, gtvwa.err_where != 0 ? gtvwa.err_where : "-", gtvwa.polls,
				gtvwa.timed_out,
				irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
				irqdev.gt_engine_intrs - e0, irqdev.gt_error_intr - r0);
		}
		for (ei = 0u; ei < gteng.n; ei++) {
			struct parity_execlists *el = &gteng.el[ei];

			kern_logf("i915: parity P6c %s verify_wa: state=%d list=%u emitted=%u "
				"mcr_skipped=%u verified=%u mismatched=%u not_verifiable=%u err=%d "
				"rq_seqno=%u krq_seqno=%u hwsp_seqno=%u | el submits=%u promotes=%u "
				"completes=%u errors=%u serial=%u wakeref_serial=%u ring_emit=%u\n",
				gteng.ge[ei].info->name, gtvwa.state[ei], gtvwa.list_count[ei],
				gtvwa.emitted[ei], gtvwa.mcr_skipped[ei], gtvwa.verified[ei],
				gtvwa.mismatched[ei], gtvwa.not_verifiable[ei], gtvwa.engine_err[ei],
				gtvwa.rq[ei].seqno, gtvwa.krq[ei].seqno,
				gteng.ge[ei].hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u],
				el->submits, el->promotes, el->completes, el->csb_errors,
				el->serial, el->wakeref_serial, gteng.kernel_ce[ei].ring.emit);
			if (rc != 0)
				parity_engine_dump(&gteng.ge[ei], el, &mmio, "verify_wa");
		}
		if (rc != 0) {
			res.outcome = PARITY_FAILED; res.error = rc;
			res.where = gtvwa.err_where != 0 ? gtvwa.err_where : "__engines_verify_workarounds";
			goto p6_fw_out;
		}
		res.last_completed = "__engines_verify_workarounds";

		/*
		 * intel_uc_init_late(): nothing without GuC.  intel_migrate_init():
		 * the migrate ppgtt (two 8 MiB windows + the PTE window that maps
		 * the windows' own page tables) and a pinned 512 KiB-ring context
		 * on the first copy engine.  Its return is not checked by the
		 * reference; nothing is submitted.
		 */
		rc = parity_intel_migrate_init(&gtmig, &gteng, &gtmem);
		gtmig_inited = 1;
		kern_logf("i915: parity P6c migrate_init: rc=%d where=%s engine=%s tables=%u "
			"windows=%llu pte_window=0x%llx exposed_pts=%u ring=%u state=%u | ggtt "
			"ring=0x%llx state=0x%llx hwsp=0x%x | lrc PDP0=%08x:%08x RING_CTL=%08x "
			"top_pd=0x%llx\n",
			rc, gtmig.err_where != 0 ? gtmig.err_where : "-",
			gtmig.has_engine ? gteng.ge[gtmig.engine_idx].info->name : "-",
			gtmig.vm.n_tables, (unsigned long long)gtmig.window_bytes,
			(unsigned long long)gtmig.pte_window, gtmig.exposed_pts,
			gtmig.ce.ring.size, gtmig.ce.state_bytes,
			(unsigned long long)gtmig.ce.ring.ggtt_offset,
			gtmig.ce.state != 0 ? (unsigned long long)gtmig.ce.state->ggtt_offset : 0ull,
			gtmig.hwsp_ggtt,
			gtmig.ce.lrc_reg_state != 0 ? gtmig.ce.lrc_reg_state[PARITY_CTX_PDP0_UDW] : 0u,
			gtmig.ce.lrc_reg_state != 0 ? gtmig.ce.lrc_reg_state[PARITY_CTX_PDP0_LDW] : 0u,
			gtmig.ce.lrc_reg_state != 0 ? gtmig.ce.lrc_reg_state[PARITY_CTX_RING_CTL] : 0u,
			(unsigned long long)gtmig.vm.top_pd_dma);
		if (rc == 0)
			res.last_completed = "intel_migrate_init";

p6_fw_out:
		osdep_fw_put(&mmio, OSDEP_FW_MEDIA_VEBOX0);
		osdep_fw_put(&mmio, OSDEP_FW_MEDIA_VDBOX2);
		osdep_fw_put(&mmio, OSDEP_FW_MEDIA_VDBOX0);
		osdep_fw_put(&mmio, OSDEP_FW_GT);
		osdep_fw_put(&mmio, OSDEP_FW_RENDER);
		if (res.outcome == PARITY_FAILED)
			goto teardown;
	}

	/*
	 * intel_gt_init() is complete.  i915_gem_init() ends with
	 * intel_engines_driver_register(): uabi class/instance names and the
	 * engine rb-tree for userspace -- bookkeeping with no HW side.
	 */
	res.last_completed = "i915_gem_init";

	/*
	 * ============================== P7 ==============================
	 * i915_driver_probe() after i915_gem_init(): intel_pxp_init(),
	 * intel_display_driver_probe(), i915_driver_register().
	 */

	/* intel_pxp_init(): full feature on ADL-P (has_pxp, VDBOX, mei-pxp path). */
	rc = parity_intel_pxp_init(&pxp, &gteng, &gtpp, &gtmem, 1 /* has_pxp */);
	pxp_inited = 1;
	kern_logf("i915: parity P7 pxp_init: rc=%d where=%s full=%d engine=%s kcr_base=0x%x "
		"ce ring=%u state=%u ggtt=0x%llx hwsp=0x%x stream_cmd=%d component_added=%d "
		"(KCR init + irq enable wait for the mei-pxp bind: none here)\n",
		rc, pxp.err_where != 0 ? pxp.err_where : "-", pxp.full_feature,
		pxp.has_engine ? gteng.ge[pxp.engine_idx].info->name : "-", pxp.kcr_base,
		pxp.ce.ring.size, pxp.ce.state_bytes,
		pxp.ce.state != 0 ? (unsigned long long)pxp.ce.state->ggtt_offset : 0ull,
		pxp.hwsp_ggtt, pxp.stream_cmd != 0, pxp.component_added);
	/* "if (ret && ret != -ENODEV) drm_dbg(pxp init failed)": never fatal. */
	res.last_completed = "intel_pxp_init";

	/* The DMC payload gates the DC_off well's disable (DC6 entry) below. */
	pwc.dmc_has_payload = dmc_inited ? parity_dmc_has_payload(&dmc_dev.dmc) : 0;

	/* intel_display_driver_probe() */
	rc = parity_intel_display_driver_probe(&dprobe, &mmio, &nogem, &power_domains,
		&pwc, (int)display_ver, pch.type, 1 /* display_irqs_enabled */,
		irqdev.irqs_enabled);
	kern_logf("i915: parity P7 display_driver_probe: rc=%d active_crtcs=%u initial_commit=%d%s "
		"overlay=%d fbdev=%d ipc_enabled=%d\n",
		rc, dprobe.active_crtcs, dprobe.initial_commit_rc,
		dprobe.initial_commit_unimplemented ? " (DRM atomic commit unimplemented)" : "",
		dprobe.overlay, dprobe.fbdev, dprobe.ipc_enabled);
	kern_logf("i915: parity P7 hpd_init: encoders=%u pins=%d,%d setups=%u skipped=%d | de: enabled=0x%x "
		"hotplug=0x%x IMR=0x%08x TC_CTL=0x%08x TBT_CTL=0x%08x | pch: enabled=0x%x hotplug=0x%x "
		"SHPD_FILTER=0x%x SDEIMR=0x%08x%s SHOTPLUG_DDI=0x%08x SHOTPLUG_TC=0x%08x | poll: works=%u "
		"core_gets=%u\n",
		dprobe.hp.n_encoders, dprobe.hp.encoder_pin[0], dprobe.hp.encoder_pin[1],
		dprobe.hp.irq_setups, dprobe.hp.irq_setup_skipped, dprobe.hp.de_enabled_irqs,
		dprobe.hp.de_hotplug_irqs, dprobe.hp.de_hpd_imr, dprobe.hp.tc_ctl, dprobe.hp.tbt_ctl,
		dprobe.hp.pch_enabled_irqs, dprobe.hp.pch_hotplug_irqs, dprobe.hp.shpd_filter,
		dprobe.hp.sdeimr, dprobe.hp.sdeimr_skipped ? "(not written: irqs off)" : "",
		dprobe.hp.shotplug_ddi, dprobe.hp.shotplug_tc, dprobe.hp.poll_init_works,
		dprobe.hp.poll_core_gets);
	/*
	 * The hotplug path (E-123): intel_hpd_init_early() + the connectors of the encoders intel_setup_outputs() made;
	 * from here on gen8_de_irq_handler() hands SDEIIR to icp_irq_handler().  (The reference makes the connectors in
	 * intel_ddi_init; adaptation: they are made here, once the hotplug registers are programmed.)
	 */
	if (rc == 0 && parity_hpd_start(&dprobe.hp, &mmio, &nogem, &power_domains, &pwc, pch.type,
			irqdev.irqs_enabled, NULL) == 0)
		parity_hpd_started = 1;
	kern_logf("i915: parity P7 hotplug path: %s\n", parity_hpd_started ? "started" : "NOT started");
	if (dprobe.initial_commit_unimplemented) {
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_UNIMPL,
			"intel_initial_commit", dprobe.active_crtcs, 0u);
		/*
		 * E-124 (N1): an ACTIVE crtc at probe time is the firmware display, and the takeover of it is
		 * ported in this build -- it runs later, from the LCD test, with the reference own readout.
		 * The reference itself only logs a failed initial modeset and continues; without the takeover
		 * this probe stops here instead, because it would otherwise leave that display untouched and
		 * unaccounted for.
		 */
		if (!PARITY_N1_TEST) {
			res.outcome = PARITY_BLOCKED;
			res.where = "intel_initial_commit";
			goto teardown;
		}
		kern_logf("i915: parity P7 intel_initial_commit: %u active crtc(s) left by the firmware; the DRM "
			"atomic commit is not ported, and N1 takes that display over later in this run\n",
			dprobe.active_crtcs);
	}
	res.last_completed = "intel_display_driver_probe";

	/* i915_driver_register() */
	/*
	 * intel_opregion_register(): E-122 VBT_ONLY -- the OpRegion runtime protocol is not joined (no ACPI notifier is
	 * registered, drdy / ardy / csts / DIDL / CADL are not written, no ASLE service), as in the reference built without
	 * ACPI where these calls are empty.  Recorded; the probe continues.
	 */
	if (opregion_present) {
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_NOTE,
			"intel_opregion_register:runtime_disabled(vbt_only)", 0u, 0u);
		kern_logf("i915: parity P7 intel_opregion_register: runtime DISABLED (VBT_ONLY, ACPI_RUNTIME_UNAVAILABLE): no "
			"notifier registered, no mailbox written, no ASLE service -- the probe continues\n");
	}
	parity_i915_driver_register(&dprobe, &dcore, &probe_pm, 0 /* opregion runtime not registered */);
	kern_logf("i915: parity P7 driver_register: na_registrations=%u opregion=%d kms_poll=%d | "
		"power_domains_enable: wells_on %u -> %u dc_state=0x%x verify_mismatches=%u | "
		"runtime_pm_enable: probe usage=%d active=%d (autosuspend not armed: "
		"intel_runtime_suspend unimplemented)\n",
		dprobe.na_registrations, dprobe.opregion_registered, dprobe.hp.kms_poll_inited,
		dprobe.wells_on_before, dprobe.wells_on_after, (unsigned)dprobe.dc_state_after,
		dprobe.verify_mismatches, dprobe.rpm_usage_after, osdep_rpm_active(&probe_pm));
	{
		unsigned wi;

		for (wi = 0u; wi < power_domains.num_power_wells; wi++) {
			struct parity_power_well *w = &power_domains.power_wells[wi];

			if (w->refcount != 0u || parity_power_well_is_enabled(w, &pwc))
				kern_logf("i915: parity P7 well %s: refcount=%u hw_enabled=%d\n",
					w->name, w->refcount, parity_power_well_is_enabled(w, &pwc));
		}
		kern_logf("i915: parity P7 DC_STATE_EN=0x%08x DC_off disable_calls=%u dc_state_writes=%u "
			"rewrites=%u dmc_has_payload=%d\n",
			osdep_mmio_read32(&mmio, 0x45504u), pwc.dc_off_disable_calls,
			pwc.dc_state_writes, pwc.dc_state_rewrites, pwc.dmc_has_payload);
	}
	res.last_completed = "i915_driver_register";

	/*
	 * ====================== real-hardware EU test (once) ======================
	 * Explicitly released: the compute positive control (GPGPU_WALKER, one
	 * SIMD8 thread, unconditional A64 store) on the GT initialised the Linux
	 * way, as an execbuf-shaped request on a fresh context.  Like a user
	 * submission after driver load: GT wakeref = forcewake all around it.
	 */
	/*
	 * Display test configuration: one real AUX acquisition from the eDP panel (PPS /
	 * VDD, DPCD, EDID) and its stop path.  No GPU submission happens in this mode.
	 * The acquisition itself now happens in intel_setup_outputs() (the reference's place);
	 * this only examines the resident panel and exercises the delayed VDD-off.
	 */
	if (PARITY_AUX_TEST) {
		(void)parity_edp_aux_test_run(&edp_dev);
		/* the scanout buffer next to the live GT resources; nothing is scanned out yet */
		if (gtmem_inited)
			(void)parity_lcd_scanout_hw_check(&gtmem);
	}

	/*
	 * LCD-B: one known picture on the panel through the reference's modeset, a finite observation window, the
	 * reference's stop path.  No GPU submission happens in this mode; the AUX diagnostics are not repeated.
	 */
	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST || PARITY_LCDO_TEST || PARITY_HDMI_B_TEST || PARITY_DUAL_TEST || PARITY_DUAL_SHARE_TEST || PARITY_N1_TEST) {
		static struct parity_lcd_kernel_deps lcdb;

		lcdb.edp = &edp_dev; lcdb.mmio = &mmio; lcdb.pd = &power_domains; lcdb.pwc = &pwc; lcdb.dcore = &dcore;
		lcdb.cdclk = &cdclk; lcdb.nogem = &nogem; lcdb.dstate = &dstate; lcdb.bw = &bw_state; lcdb.dmc = &dmc_dev;
		lcdb.irq = &irqdev; lcdb.gm = &gtmem; lcdb.ipc_enabled = dprobe.ipc_enabled;
		lcdb.es = &gteng; lcdb.vm = &gtpp; lcdb.uncore_lock = &uncore_lock;
		lcdb.gmadr_base = n0_gmadr_base; lcdb.gmadr_size = n0_gmadr_size; lcdb.dprobe = &dprobe;
		if (gtmem_inited) {
			struct parity_lcd_test_summary sum;

			if (PARITY_LCDG_TEST || PARITY_LCDD_TEST) {
				/* like the TEX test: the GT's forcewake domains are held around the submission */
				static const int gfwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
					OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
				unsigned gheld = 0u;
				int gfrc = 0;

				while (gheld < 5u && (gfrc = osdep_fw_get(&mmio, gfwd[gheld])) == 0)
					gheld++;
				if (gheld == 5u && PARITY_LCDD_TEST)
					(void)parity_lcd_kernel_lcdd_run(&lcdb);
				else if (gheld == 5u)
					(void)parity_lcd_kernel_lcdg_run(&lcdb);
				else
					kern_logf("i915: parity LCD-%c verdict: FAIL (forcewake rc=%d; nothing submitted)\n",
						PARITY_LCDD_TEST ? 'D' : 'G', gfrc);
				while (gheld-- > 0u)
					osdep_fw_put(&mmio, gfwd[gheld]);
			} else if (PARITY_N1_TEST) {
				(void)parity_lcd_kernel_n1_run(&lcdb);
			} else if (PARITY_DUAL_SHARE_TEST) {
				(void)parity_lcd_kernel_dual_share_run(&lcdb);
			} else if (PARITY_DUAL_TEST) {
				(void)parity_lcd_kernel_dual_run(&lcdb);
			} else if (PARITY_HDMI_B_TEST) {
				(void)parity_lcd_kernel_hdmib_run(&lcdb);
			} else if (PARITY_LCDO_TEST) {
				(void)parity_lcd_kernel_lcdo_run(&lcdb);
			} else if (PARITY_LCDC_TEST) {
				(void)parity_lcd_kernel_lcdc_run(&lcdb);
			} else if (PARITY_LCDR_TEST)
				(void)parity_lcd_kernel_lcdr_run(&lcdb);
			else
				(void)parity_lcd_kernel_lcdb_run(&lcdb);
			parity_lcd_kernel_summary(&sum);
			res.lcd_test_ran = 1;
			res.lcd_test_pass = sum.pass;
			res.lcd_cleanup_rc = sum.cleanup_rc;
			res.lcd_retained = sum.retained;
			res.lcd_first_anomaly_stage = sum.first_anomaly != 0 ? sum.first_anomaly_stage : "none";
		} else {
			kern_logf("i915: parity LCD-B verdict: FAIL (no GT memory)\n");
			res.lcd_test_ran = 1;
			res.lcd_first_anomaly_stage = "no-gt-memory";
		}
	}

	/*
	 * E-124 (N1): the takeover has run, so the INIT reference that kept the firmware display alive is
	 * released now -- the point intel_power_domains_enable() would have done it.
	 */
	if (dprobe.power_domains_enable_deferred) {
		parity_intel_power_domains_enable(&dprobe, &dcore);
		kern_logf("i915: parity P7 power_domains_enable (deferred until after N1): wells_on %u -> %u "
			"dc_state=0x%x verify_mismatches=%u\n", dprobe.wells_on_before, dprobe.wells_on_after,
			(unsigned)dprobe.dc_state_after, dprobe.verify_mismatches);
	}

	/* HPD-TEST: the HDMI cable of DDI B is plugged / unplugged in a finite window (no GPU submission) */
	if (PARITY_HDMI_EDID_TEST) {
		if (parity_hpd_started)
			(void)parity_hdmi_edid_test_run(&mmio);
		else
			kern_logf("i915: parity HDMI-EDID verdict: FAIL (the hotplug path did not start)\n");
	}
	if (PARITY_HDMI_HPD_TEST) {
		if (parity_hpd_started)
			(void)parity_hpd_test_run(&mmio, 240u);
		else
			kern_logf("i915: parity HPD-TEST verdict: FAIL (the hotplug path did not start)\n");
	}

	if (PARITY_T3_TEST || PARITY_BL_TEST) {
		const char *t3tag = PARITY_BL_TEST ? "BL" : "T3";
		static const int t3fwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
			OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
		unsigned held = 0u, wi;
		int frc = 0;

		while (held < 5u && (frc = osdep_fw_get(&mmio, t3fwd[held])) == 0)
			held++;
		if (held == 5u) {
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_error_intr;

			rc = PARITY_BL_TEST ?
				parity_bl_test_run(&t3test, &gteng, &gtpp, &gtmem, &mmio, &uncore_lock, 2000u) :
				parity_t3_test_run(&t3test, &gteng, &gtpp, &gtmem, &mmio, &uncore_lock, 2000u);
			t3test_inited = 1;
			kern_logf("i915: parity %s fixture: build PARITY_T3_TEST=%d PARITY_BL_TEST=%d batch_hash=%016llx batch_dwords=%u mocs=%u "
				"state@0x%llx batch@0x%llx rt@0x%llx texA@0x%llx texB@0x%llx | pipeline_select rc=%d 3d=%u gpgpu=%u bad=%u\n",
				t3tag, PARITY_T3_TEST, PARITY_BL_TEST, (unsigned long long)t3test.t.batch_hash, t3test.t.batch_dwords,
				t3test.mocs, (unsigned long long)PARITY_EU_SHARED_VA,
				(unsigned long long)PARITY_EU_BATCH_VA, (unsigned long long)PARITY_DRAW_RT_VA,
				(unsigned long long)I915_TEX_FIXTURE_TEX_VA, (unsigned long long)I915_TEX_FIXTURE_TEX_B_VA,
				t3test.t.pipesel_rc, t3test.t.pipesel.n_3d, t3test.t.pipesel.n_gpgpu,
				t3test.t.pipesel.n_bad);
			for (wi = 0u; wi < t3test.n_steps; wi++) {
				const struct parity_t3_step *st = &t3test.step[wi];

				kern_logf("i915: parity %s step=%u ctx=%c bind=%c upload=%c:%d expect_variant=%d filter=%s "
					"differs_from_nearest=%u max_channel_diff=%u lrca=%08x seqno=%u "
					"hwsp_observed=%u rc=%d completed=%d parked=%d pass=%d polls=%u | markers=%08x %08x %08x ps=%08x | "
					"pixels match=%u/1024 stale=%u first_bad=(%d,%d) expected=%08x observed=%08x | tex changed=%u "
					"guard_bad=%u | state_hash=%016llx rt_hash=%016llx texA_hash=%016llx texB_hash=%016llx\n",
					t3tag, wi + 1u, st->ctx, st->bind, st->upload, st->upload_variant, st->expect_variant,
					st->linear ? "linear" : "nearest", st->differs_from_nearest, st->max_channel_diff,
					st->lrca, st->seqno, st->hwsp_observed, st->rc, st->completed, st->parked,
					st->pass, st->polls, st->before, st->middraw, st->after, st->ps_marker,
					st->px_match, st->px_stale, st->first_bad_x, st->first_bad_y,
					st->first_bad_expected, st->first_bad_observed, st->tex_changed_bytes,
					st->guard_bad_bytes, (unsigned long long)st->state_hash,
					(unsigned long long)st->rt_hash, (unsigned long long)st->tex_a_hash,
					(unsigned long long)st->tex_b_hash);
			}
			/* The last step's objects, in the TEX-TEST dump format (tools/eu_artifact.py). */
			if (t3test.t.batch != 0 && t3test.t.batch_dwords != 0u) {
				const uint32_t *bd = (const uint32_t *)t3test.t.batch->cpu;

				for (wi = 0u; wi < t3test.t.batch_dwords; wi += 8u)
					kern_logf("i915: parity %s-LAST batch[%03u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						t3tag, wi, bd[wi], bd[wi + 1u], bd[wi + 2u], bd[wi + 3u], bd[wi + 4u],
						bd[wi + 5u], bd[wi + 6u], bd[wi + 7u]);
			}
			if (t3test.t.shared != 0) {
				const uint32_t *sp = (const uint32_t *)t3test.t.shared->cpu;

				for (wi = 0u; wi < 1024u; wi += 8u) {
					if ((sp[wi] | sp[wi + 1u] | sp[wi + 2u] | sp[wi + 3u] | sp[wi + 4u] |
					     sp[wi + 5u] | sp[wi + 6u] | sp[wi + 7u]) == 0u)
						continue;
					kern_logf("i915: parity %s-LAST state[%04u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						t3tag, wi, sp[wi], sp[wi + 1u], sp[wi + 2u], sp[wi + 3u], sp[wi + 4u],
						sp[wi + 5u], sp[wi + 6u], sp[wi + 7u]);
				}
			}
			if (t3test.tex_b != 0) {
				const uint32_t *tp = (const uint32_t *)t3test.tex_b->cpu;

				for (wi = 0u; wi < 64u; wi += 8u)
					kern_logf("i915: parity %s-LAST tex[%02u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						t3tag, wi, tp[wi], tp[wi + 1u], tp[wi + 2u], tp[wi + 3u], tp[wi + 4u],
						tp[wi + 5u], tp[wi + 6u], tp[wi + 7u]);
			}
			if (t3test.rt != 0) {
				const uint32_t *rp = (const uint32_t *)t3test.rt->cpu;

				for (wi = 0u; wi < 1024u; wi += 8u)
					kern_logf("i915: parity %s-LAST rt[%04u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						t3tag, wi, rp[wi], rp[wi + 1u], rp[wi + 2u], rp[wi + 3u], rp[wi + 4u],
						rp[wi + 5u], rp[wi + 6u], rp[wi + 7u]);
			}
			kern_logf("i915: parity %s-LAST fixture: batch_dwords=%u\n", t3tag, t3test.t.batch_dwords);
			kern_logf("i915: parity %s %s: rc=%d where=%s steps=%u/%u passed=%u wedged=%d polls=%u | "
				"gt irq: user=%u ctx_switch=%u error=%u\n",
				t3tag, (rc == 0 && t3test.passed == t3test.n_planned) ? "PASS" :
				t3test.t.wedged ? "HANG" : "ERROR",
				rc, t3test.t.err_where != 0 ? t3test.t.err_where : "-", t3test.n_steps,
				t3test.n_planned, t3test.passed, t3test.t.wedged, t3test.t.polls,
				irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
				irqdev.gt_error_intr - e0);
		} else {
			kern_logf("i915: parity %s not run: forcewake failed rc=%d\n", t3tag, frc);
		}
		while (held-- > 0u)
			osdep_fw_put(&mmio, t3fwd[held]);
	}

	if (PARITY_TEX_TEST && !PARITY_T3_TEST && !PARITY_BL_TEST) {
		static const int tfwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
			OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
		unsigned held = 0u, wi;
		int frc = 0;

		while (held < 5u && (frc = osdep_fw_get(&mmio, tfwd[held])) == 0)
			held++;
		if (held == 5u) {
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_error_intr;
			const struct parity_eu_test *tt = &textest.t;

			rc = parity_tex_test_run(&textest, &gteng, &gtpp, &gtmem, &mmio, &uncore_lock, 2000u);
			textest_inited = 1;
			kern_logf("i915: parity TEX-TEST fixture: build PARITY_TEX_TEST=%d batch_hash=%016llx state_hash=%016llx "
				"tex_hash=%016llx batch_dwords=%u mocs=%u pdp0_matches_top=%d | pipeline_select (read from the "
				"submitted object): rc=%d 3d=%u@%u gpgpu=%u bad=%u\n",
				PARITY_TEX_TEST, (unsigned long long)tt->batch_hash,
				(unsigned long long)textest.state_hash, (unsigned long long)textest.tex_hash,
				tt->batch_dwords, textest.mocs, tt->pdp0_matches_top, tt->pipesel_rc,
				tt->pipesel.n_3d, tt->pipesel.idx_3d, tt->pipesel.n_gpgpu, tt->pipesel.n_bad);
			for (wi = 0u; wi < tt->walks; wi++) {
				const struct parity_gt_ppgtt_walk *w = &tt->walk[wi];

				kern_logf("i915: parity TEX-TEST walk[%u] va=0x%llx levels=%d leaf=0x%llx present=%d rw=%d pat=%u scr=%d\n",
					wi, (unsigned long long)w->va, w->levels, (unsigned long long)w->leaf_dma,
					w->leaf_present, w->leaf_rw, w->leaf_pat, w->scratch[3]);
			}
			if (tt->batch != 0 && tt->batch_dwords != 0u) {
				const uint32_t *bd = (const uint32_t *)tt->batch->cpu;

				for (wi = 0u; wi < tt->batch_dwords; wi += 8u)
					kern_logf("i915: parity TEX-TEST batch[%03u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, bd[wi], bd[wi + 1u], bd[wi + 2u], bd[wi + 3u], bd[wi + 4u],
						bd[wi + 5u], bd[wi + 6u], bd[wi + 7u]);
			}
			if (tt->shared != 0) {
				const uint32_t *sp = (const uint32_t *)tt->shared->cpu;

				for (wi = 0u; wi < 1024u; wi += 8u) {
					if ((sp[wi] | sp[wi + 1u] | sp[wi + 2u] | sp[wi + 3u] | sp[wi + 4u] |
					     sp[wi + 5u] | sp[wi + 6u] | sp[wi + 7u]) == 0u)
						continue;
					kern_logf("i915: parity TEX-TEST state[%04u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, sp[wi], sp[wi + 1u], sp[wi + 2u], sp[wi + 3u], sp[wi + 4u],
						sp[wi + 5u], sp[wi + 6u], sp[wi + 7u]);
				}
			}
			if (textest.tex != 0) {
				const uint32_t *tp = (const uint32_t *)textest.tex->cpu;

				for (wi = 0u; wi < 64u; wi += 8u)
					kern_logf("i915: parity TEX-TEST tex[%02u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, tp[wi], tp[wi + 1u], tp[wi + 2u], tp[wi + 3u], tp[wi + 4u],
						tp[wi + 5u], tp[wi + 6u], tp[wi + 7u]);
			}
			if (textest.rt != 0) {
				const uint32_t *rp = (const uint32_t *)textest.rt->cpu;

				for (wi = 0u; wi < 1024u; wi += 8u)
					kern_logf("i915: parity TEX-TEST rt[%04u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, rp[wi], rp[wi + 1u], rp[wi + 2u], rp[wi + 3u], rp[wi + 4u],
						rp[wi + 5u], rp[wi + 6u], rp[wi + 7u]);
			}
			if (textest.stats_valid)
				kern_logf("i915: parity TEX-TEST stats(live, before reset): ia_vertices=%u ia_primitives=%u "
					"vs_invocations=%u cl_invocations=%u cl_primitives=%u ps_invocations=%u\n",
					textest.stats_live[0], textest.stats_live[1], textest.stats_live[2],
					textest.stats_live[3], textest.stats_live[4], textest.stats_live[5]);
			kern_logf("i915: parity TEX-TEST %s: rc=%d where=%s engine=%s batch_dwords=%u submitted=%d completed=%d "
				"parked=%d timed_out=%d wedged=%d polls=%u | before=%08x middraw=%08x after=%08x ps_marker=%08x | "
				"pixels match=%u/%u stale=%u first_bad=(%d,%d) expected=%08x observed=%08x | texture changed_bytes=%u "
				"guard_bad_bytes=%u | rq seqno=%u krq seqno=%u | gt irq: user=%u ctx_switch=%u error=%u\n",
				tt->outcome == PARITY_EU_PASS ? "PASS" :
				tt->outcome == PARITY_EU_HANG ? "HANG" : "ERROR",
				rc, tt->err_where != 0 ? tt->err_where : "-",
				gteng.ge[tt->engine_idx].info->name, tt->batch_dwords, tt->submitted,
				tt->completed, tt->parked, tt->timed_out, tt->wedged, tt->polls,
				textest.marker_before, textest.marker_middraw, textest.marker_after,
				textest.ps_marker, textest.px_match, textest.px_total, textest.px_stale,
				textest.first_bad_x, textest.first_bad_y, textest.first_bad_expected,
				textest.first_bad_observed, textest.tex_changed_bytes, textest.guard_bad_bytes,
				tt->rq.seqno, tt->krq.seqno, irqdev.gt_user_intr - u0,
				irqdev.gt_ctx_switch_intr - c0, irqdev.gt_error_intr - e0);
		} else {
			kern_logf("i915: parity TEX-TEST not run: forcewake failed rc=%d\n", frc);
		}
		while (held-- > 0u)
			osdep_fw_put(&mmio, tfwd[held]);
	}

	if (PARITY_R1_TEST && !PARITY_TEX_TEST && !PARITY_T3_TEST && !PARITY_BL_TEST) {
		static const int rfwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
			OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
		unsigned held = 0u, wi;
		int frc = 0;

		while (held < 5u && (frc = osdep_fw_get(&mmio, rfwd[held])) == 0)
			held++;
		if (held == 5u) {
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_error_intr;

			rc = parity_r1_test_run(&r1test, &gteng, &gtpp, &gtmem, &gtmmio.sseu, &mmio,
				&uncore_lock, 2000u);
			r1test_inited = 1;
			kern_logf("i915: parity R1 fixture: build PARITY_R1_TEST=%d c1_batch_hash=%016llx (dwords=%u @0x%llx) "
				"draw_batch_hash=%016llx (dwords=%u @0x%llx) state@0x%llx rt@0x%llx mocs=%u\n",
				PARITY_R1_TEST, (unsigned long long)r1test.c1_batch_hash, r1test.t.batch_dwords,
				(unsigned long long)PARITY_EU_BATCH_VA, (unsigned long long)r1test.draw_batch_hash,
				r1test.dbatch_dwords, (unsigned long long)PARITY_R1_DRAW_BATCH_VA,
				(unsigned long long)PARITY_EU_SHARED_VA, (unsigned long long)PARITY_DRAW_RT_VA,
				r1test.mocs);
			for (wi = 0u; wi < r1test.n_steps; wi++) {
				const struct parity_r1_step *st = &r1test.step[wi];

				if (st->kind == 'D')
					kern_logf("i915: parity R1 step=%u ctx=%c kind=draw lrca=%08x seqno=%u hwsp_observed=%u rc=%d "
						"completed=%d parked=%d pass=%d polls=%u state_hash=%016llx | before=%08x middraw=%08x "
						"after=%08x ps_marker=%08x | pixels match=%u/1024 stale=%u first=%08x last=%08x\n",
						wi + 1u, st->ctx, st->lrca, st->seqno, st->hwsp_observed, st->rc,
						st->completed, st->parked, st->pass, st->polls,
						(unsigned long long)st->state_hash, st->before, st->middraw, st->after,
						st->ps_marker, st->px_match, st->px_stale, st->px_first, st->px_last);
				else
					kern_logf("i915: parity R1 step=%u ctx=%c kind=c1 lrca=%08x seqno=%u hwsp_observed=%u rc=%d "
						"completed=%d parked=%d pass=%d polls=%u state_hash=%016llx | ready=%08x eu=%08x "
						"done=%08x cs=%08x idd_rb_ok=%d kernel_rb_ok=%d\n",
						wi + 1u, st->ctx, st->lrca, st->seqno, st->hwsp_observed, st->rc,
						st->completed, st->parked, st->pass, st->polls,
						(unsigned long long)st->state_hash, st->ready, st->eu, st->done, st->cs,
						st->idd_rb_ok, st->kernel_rb_ok);
			}
			kern_logf("i915: parity R1 %s: rc=%d where=%s steps=%u/%u passed=%u wedged=%d polls=%u | "
				"gt irq: user=%u ctx_switch=%u error=%u\n",
				(rc == 0 && r1test.passed == r1test.n_planned) ? "PASS" :
				r1test.t.wedged ? "HANG" : "ERROR",
				rc, r1test.t.err_where != 0 ? r1test.t.err_where : "-", r1test.n_steps,
				r1test.n_planned, r1test.passed, r1test.t.wedged, r1test.t.polls,
				irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
				irqdev.gt_error_intr - e0);
		} else {
			kern_logf("i915: parity R1 not run: forcewake failed rc=%d\n", frc);
		}
		while (held-- > 0u)
			osdep_fw_put(&mmio, rfwd[held]);
	}

	if (PARITY_DRAW_TEST && !PARITY_R1_TEST && !PARITY_TEX_TEST && !PARITY_T3_TEST && !PARITY_BL_TEST) {
		static const int dfwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
			OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
		unsigned held = 0u, wi;
		int frc = 0;

		while (held < 5u && (frc = osdep_fw_get(&mmio, dfwd[held])) == 0)
			held++;
		if (held == 5u) {
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_error_intr;
			const struct parity_eu_test *dt = &drawtest.t;

			rc = parity_draw_test_run(&drawtest, &gteng, &gtpp, &gtmem, &mmio, &uncore_lock, 2000u);
			drawtest_inited = 1;
			kern_logf("i915: parity DRAW-TEST fixture: build PARITY_DRAW_TEST=%d batch_hash=%016llx "
				"state_hash=%016llx batch_dwords=%u mocs=%u pdp0_matches_top=%d | pipeline_select "
				"(read from the submitted object): rc=%d 3d=%u@%u gpgpu=%u bad=%u@%u bad_word=%08x\n",
				PARITY_DRAW_TEST, (unsigned long long)dt->batch_hash,
				(unsigned long long)drawtest.state_hash, dt->batch_dwords, drawtest.mocs,
				dt->pdp0_matches_top, dt->pipesel_rc, dt->pipesel.n_3d, dt->pipesel.idx_3d,
				dt->pipesel.n_gpgpu, dt->pipesel.n_bad, dt->pipesel.idx_bad, dt->pipesel.bad_word);
			for (wi = 0u; wi < dt->walks; wi++) {
				const struct parity_gt_ppgtt_walk *w = &dt->walk[wi];

				kern_logf("i915: parity DRAW-TEST walk[%u] levels=%d leaf=0x%llx present=%d rw=%d pat=%u scr=%d\n",
					wi, w->levels, (unsigned long long)w->leaf_dma, w->leaf_present,
					w->leaf_rw, w->leaf_pat, w->scratch[3]);
			}
			if (dt->batch != 0 && dt->batch_dwords != 0u) {
				const uint32_t *bd = (const uint32_t *)dt->batch->cpu;

				for (wi = 0u; wi < dt->batch_dwords; wi += 8u)
					kern_logf("i915: parity DRAW-TEST batch[%03u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, bd[wi], bd[wi + 1u], bd[wi + 2u], bd[wi + 3u], bd[wi + 4u],
						bd[wi + 5u], bd[wi + 6u], bd[wi + 7u]);
			}
			if (dt->shared != 0) {
				const uint32_t *sp = (const uint32_t *)dt->shared->cpu;

				/* The state page as the GPU saw it (rows that are all zero are skipped). */
				for (wi = 0u; wi < 1024u; wi += 8u) {
					if ((sp[wi] | sp[wi + 1u] | sp[wi + 2u] | sp[wi + 3u] | sp[wi + 4u] |
					     sp[wi + 5u] | sp[wi + 6u] | sp[wi + 7u]) == 0u)
						continue;
					kern_logf("i915: parity DRAW-TEST state[%04u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, sp[wi], sp[wi + 1u], sp[wi + 2u], sp[wi + 3u], sp[wi + 4u],
						sp[wi + 5u], sp[wi + 6u], sp[wi + 7u]);
				}
			}
			if (drawtest.stats_valid)
				kern_logf("i915: parity DRAW-TEST stats(live, before reset): ia_vertices=%u ia_primitives=%u "
					"vs_invocations=%u cl_invocations=%u cl_primitives=%u ps_invocations=%u\n",
					drawtest.stats_live[0], drawtest.stats_live[1], drawtest.stats_live[2],
					drawtest.stats_live[3], drawtest.stats_live[4], drawtest.stats_live[5]);
			kern_logf("i915: parity DRAW-TEST %s: rc=%d where=%s engine=%s batch_dwords=%u submitted=%d "
				"completed=%d parked=%d timed_out=%d wedged=%d polls=%u | before=%08x middraw=%08x after=%08x "
				"ps_marker=%08x | pixels match=%u/%u first=%08x mid=%08x last=%08x expected=%08x | "
				"rq seqno=%u krq seqno=%u | gt irq: user=%u ctx_switch=%u error=%u\n",
				dt->outcome == PARITY_EU_PASS ? "PASS" :
				dt->outcome == PARITY_EU_HANG ? "HANG" : "ERROR",
				rc, dt->err_where != 0 ? dt->err_where : "-",
				gteng.ge[dt->engine_idx].info->name, dt->batch_dwords, dt->submitted,
				dt->completed, dt->parked, dt->timed_out, dt->wedged, dt->polls,
				drawtest.marker_before, drawtest.marker_middraw, drawtest.marker_after,
				drawtest.ps_marker, drawtest.px_match, drawtest.px_total, drawtest.px_first,
				drawtest.px_mid, drawtest.px_last, 0xffff0000u, dt->rq.seqno, dt->krq.seqno,
				irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
				irqdev.gt_error_intr - e0);
		} else {
			kern_logf("i915: parity DRAW-TEST not run: forcewake failed rc=%d\n", frc);
		}
		while (held-- > 0u)
			osdep_fw_put(&mmio, dfwd[held]);
	}

	if (PARITY_EU_TEST && !PARITY_DRAW_TEST && !PARITY_R1_TEST && !PARITY_TEX_TEST && !PARITY_T3_TEST && !PARITY_BL_TEST) {
		static const int fwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
			OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
		unsigned ti, held = 0u, k;
		int frc = 0;

		while (held < 5u && (frc = osdep_fw_get(&mmio, fwd[held])) == 0)
			held++;
		if (held == 5u) {
			unsigned u0 = irqdev.gt_user_intr, c0 = irqdev.gt_ctx_switch_intr;
			unsigned e0 = irqdev.gt_error_intr;
			static struct parity_mcr_probe mcrp;   /* off the 16 KiB stack */
			unsigned wi;

			/* MCR readback: post-init, pre-submission, RCS engine list. */
			(void)parity_mcr_probe_wa(&mcrp, &mmio, &gtinit.engine_wa[0], &gtmmio.sseu);
			for (wi = 0u; wi < mcrp.n; wi++)
				kern_logf("i915: parity MCR-PROBE phase=pre-eu reg=0x%05x group=%u instance=%u "
					"raw=0x%08x expected_set=0x%08x read_mask=0x%08x masked_mismatch=0x%08x "
					"selector_before=0x%08x selector_after=0x%08x listed=%d\n",
					mcrp.e[wi].reg, mcrp.e[wi].group, mcrp.e[wi].instance, mcrp.e[wi].raw,
					mcrp.e[wi].expected_set, mcrp.e[wi].read_mask, mcrp.e[wi].masked_mismatch,
					mcrp.e[wi].selector_before, mcrp.e[wi].selector_after, mcrp.e[wi].listed);
			kern_logf("i915: parity MCR-PROBE summary: entries=%u mismatches=%u lock_rc=%d "
				"subslice_mask=0x%x\n", mcrp.n, mcrp.mismatches, mcrp.lock_rc,
				gtmmio.sseu.subslice_mask);

			rc = parity_eu_test_run(&eutest, &gteng, &gtpp, &gtmem, &gtmmio.sseu,
				&mmio, &uncore_lock, 2000u);
			eutest_inited = 1;
			kern_logf("i915: parity EU-TEST fixture: build PARITY_EU_TEST=%d batch_hash=%016llx "
				"fixture_hash=%016llx batch_dwords=%u pdp0_matches_top=%d\n",
				PARITY_EU_TEST, (unsigned long long)eutest.batch_hash,
				(unsigned long long)eutest.fixture_hash, eutest.batch_dwords,
				eutest.pdp0_matches_top);
			for (wi = 0u; wi < eutest.walks; wi++) {
				const struct parity_gt_ppgtt_walk *w = &eutest.walk[wi];

				kern_logf("i915: parity EU-TEST walk va=0x%llx top=0x%llx levels=%d | "
					"PML4[%u]=%016llx child=0x%llx known=%d scr=%d | PDP[%u]=%016llx child=0x%llx "
					"known=%d scr=%d | PD[%u]=%016llx child=0x%llx known=%d scr=%d | "
					"PT[%u]=%016llx leaf=0x%llx present=%d rw=%d pat=%u scr=%d\n",
					(unsigned long long)w->va, (unsigned long long)w->top_dma, w->levels,
					w->idx[0], (unsigned long long)w->raw[0], (unsigned long long)w->child_dma[0],
					w->child_known[0], w->scratch[0],
					w->idx[1], (unsigned long long)w->raw[1], (unsigned long long)w->child_dma[1],
					w->child_known[1], w->scratch[1],
					w->idx[2], (unsigned long long)w->raw[2], (unsigned long long)w->child_dma[2],
					w->child_known[2], w->scratch[2],
					w->idx[3], (unsigned long long)w->raw[3], (unsigned long long)w->leaf_dma,
					w->leaf_present, w->leaf_rw, w->leaf_pat, w->scratch[3]);
			}
			kern_logf("i915: parity EU-TEST pipeline_select (read from the submitted object): rc=%d "
				"3d=%u@%u gpgpu=%u@%u bad=%u@%u bad_word=%08x\n",
				eutest.pipesel_rc, eutest.pipesel.n_3d, eutest.pipesel.idx_3d,
				eutest.pipesel.n_gpgpu, eutest.pipesel.idx_gpgpu, eutest.pipesel.n_bad,
				eutest.pipesel.idx_bad, eutest.pipesel.bad_word);
			if (eutest.shared != 0) {
				const uint32_t *sp = (const uint32_t *)eutest.shared->cpu;
				unsigned fk;

				kern_logf("i915: parity EU-TEST fixture-idd[@%u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
					PARITY_EU_IDD_OFFSET, sp[224], sp[225], sp[226], sp[227], sp[228], sp[229], sp[230], sp[231]);
				for (fk = 0u; fk < PARITY_EU_KERNEL_DWORDS; fk += 6u)
					kern_logf("i915: parity EU-TEST fixture-kernel[%02u]: %08x %08x %08x %08x %08x %08x\n",
						fk, sp[256u + fk], sp[257u + fk], sp[258u + fk], sp[259u + fk],
						sp[260u + fk], sp[261u + fk]);
			}
			if (eutest.batch != 0 && eutest.batch_dwords != 0u) {
				const uint32_t *bd = (const uint32_t *)eutest.batch->cpu;

				for (wi = 0u; wi < eutest.batch_dwords; wi += 8u)
					kern_logf("i915: parity EU-TEST batch[%03u]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
						wi, bd[wi], bd[wi + 1], bd[wi + 2], bd[wi + 3], bd[wi + 4], bd[wi + 5],
						bd[wi + 6], bd[wi + 7]);
			}
			kern_logf("i915: parity EU-TEST %s: rc=%d where=%s engine=%s dss=%u max_threads=%u "
				"batch_dwords=%u submitted=%d completed=%d parked=%d timed_out=%d wedged=%d "
				"polls=%u | ready=%08x eu=%08x done=%08x cs=%08x idd_rb_ok=%d kernel_rb_ok=%d "
				"| rq seqno=%u krq seqno=%u | gt irq: user=%u ctx_switch=%u error=%u\n",
				eutest.outcome == PARITY_EU_PASS ? "PASS" :
				eutest.outcome == PARITY_EU_HANG ? "HANG" : "ERROR",
				rc, eutest.err_where != 0 ? eutest.err_where : "-",
				gteng.ge[eutest.engine_idx].info->name, eutest.dss_count,
				eutest.max_threads, eutest.batch_dwords, eutest.submitted,
				eutest.completed, eutest.parked, eutest.timed_out, eutest.wedged,
				eutest.polls, eutest.ready, eutest.eu, eutest.done, eutest.cs,
				eutest.idd_rb_ok, eutest.kernel_rb_ok, eutest.rq.seqno, eutest.krq.seqno,
				irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
				irqdev.gt_error_intr - e0);
			kern_logf("i915: parity EU-TEST ctx: CTX_CTRL=%08x RING_CTL=%08x PDP0=%08x:%08x "
				"ring ggtt=0x%llx state ggtt=0x%llx | idd_rb=%08x %08x %08x %08x %08x %08x %08x %08x\n",
				eutest.ce.lrc_reg_state != 0 ? eutest.ce.lrc_reg_state[PARITY_CTX_CONTEXT_CONTROL] : 0u,
				eutest.ce.lrc_reg_state != 0 ? eutest.ce.lrc_reg_state[PARITY_CTX_RING_CTL] : 0u,
				eutest.ce.lrc_reg_state != 0 ? eutest.ce.lrc_reg_state[PARITY_CTX_PDP0_UDW] : 0u,
				eutest.ce.lrc_reg_state != 0 ? eutest.ce.lrc_reg_state[PARITY_CTX_PDP0_LDW] : 0u,
				(unsigned long long)eutest.ce.ring.ggtt_offset,
				eutest.ce.state != 0 ? (unsigned long long)eutest.ce.state->ggtt_offset : 0ull,
				eutest.idd_rb[0], eutest.idd_rb[1], eutest.idd_rb[2], eutest.idd_rb[3],
				eutest.idd_rb[4], eutest.idd_rb[5], eutest.idd_rb[6], eutest.idd_rb[7]);
			for (k = 0u; k < 36u; k += 12u)
				kern_logf("i915: parity EU-TEST kernel_rb[%u..]: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
					k, eutest.kernel_rb[k], eutest.kernel_rb[k + 1], eutest.kernel_rb[k + 2],
					eutest.kernel_rb[k + 3], eutest.kernel_rb[k + 4], eutest.kernel_rb[k + 5],
					eutest.kernel_rb[k + 6], eutest.kernel_rb[k + 7], eutest.kernel_rb[k + 8],
					eutest.kernel_rb[k + 9], eutest.kernel_rb[k + 10], eutest.kernel_rb[k + 11]);
			/* E-100: the same C1 again -- 3 more on this context, then 2 on a new context. */
			if (eutest.outcome == PARITY_EU_PASS) {
				int rrc = parity_eu_test_repeat(&eutest, &gteng, &gtpp, &gtmem, &mmio,
					&uncore_lock, 2000u, 3u, 2u);

				for (wi = 0u; wi < eutest.n_rounds; wi++) {
					const struct parity_eu_round *rd = &eutest.round[wi];

					kern_logf("i915: parity EU-REPEAT round=%u ctx=%c lrca=%08x seqno=%u hwsp_observed=%u "
						"rc=%d completed=%d parked=%d pass=%d polls=%u | ready=%08x eu=%08x done=%08x cs=%08x "
						"idd_rb_ok=%d kernel_rb_ok=%d | ring head=0x%x tail=0x%x last_csb=%08x:%08x\n",
						wi + 1u, rd->ctx, rd->lrca, rd->seqno, rd->hwsp_observed, rd->rc,
						rd->completed, rd->parked, rd->pass, rd->polls, rd->ready, rd->eu,
						rd->done, rd->cs, rd->idd_rb_ok, rd->kernel_rb_ok, rd->ring_head,
						rd->ring_tail, rd->csb_hi, rd->csb_lo);
				}
				kern_logf("i915: parity EU-REPEAT %s: rc=%d where=%s rounds=%u passed=%u (same-context 3 + new-context 2) "
					"wedged=%d | gt irq: user=%u ctx_switch=%u error=%u\n",
					(rrc == 0 && eutest.rounds_passed == 5u) ? "PASS" :
					eutest.wedged ? "HANG" : "ERROR",
					rrc, eutest.err_where != 0 ? eutest.err_where : "-",
					eutest.n_rounds, eutest.rounds_passed, eutest.wedged,
					irqdev.gt_user_intr - u0, irqdev.gt_ctx_switch_intr - c0,
					irqdev.gt_error_intr - e0);
			}
			(void)ti;
		} else {
			kern_logf("i915: parity EU-TEST not run: forcewake failed rc=%d\n", frc);
		}
		while (held-- > 0u)
			osdep_fw_put(&mmio, fwd[held]);
	}

	/* i915_driver_probe() returns 0: the driver is loaded. */
	res.outcome = PARITY_STOPPED;
	res.where = "i915_driver_probe complete";

teardown:
	/*
	 * Before anything else is given back: nothing may still be scanned out.  The driver's own stop path is
	 * the reference's; this is the test environment's last resort (a pipe left running keeps reading memory
	 * after the driver is gone, which hangs the host's IOMMU unmap when the guest ends).
	 */
	{
		unsigned forced = parity_lcd_last_resort_stop(&mmio);

		if (forced != 0u)
			kern_logf("i915: parity teardown: LAST-RESORT stopped %u display element(s) the driver had not "
				"released\n", forced);
	}
	/* the hotplug path first: its IRQ entry closed, the works cancelled (intel_hpd_cancel_work) */
	if (parity_hpd_started) {
		parity_hpd_stop();
		parity_hpd_started = 0;
	}
	/*
	 * i915_driver_remove() -> i915_driver_unregister(): runtime PM back to
	 * the core, the INIT power reference taken again (DC states off, wells
	 * on), display unregister, intel_pxp_fini.
	 */
	if (dprobe.registered) {
		unsigned wi, won = 0u;

		parity_i915_driver_unregister(&dprobe, &dcore, &probe_pm);
		for (wi = 0u; wi < power_domains.num_power_wells; wi++)
			if (parity_power_well_is_enabled(&power_domains.power_wells[wi], &pwc))
				won++;
		kern_logf("i915: parity teardown: driver_unregister (rpm usage=%d, INIT reference "
			"re-taken: wells_on=%u DC_STATE_EN=0x%08x)\n",
			osdep_rpm_usage(&probe_pm), won, osdep_mmio_read32(&mmio, 0x45504u));
	}
	if (pxp_inited) {
		parity_intel_pxp_fini(&pxp, &gtmem);
		kern_logf("i915: parity teardown: intel_pxp_fini (VCS context + stream page released)\n");
	}
	/*
	 * Stopping the diagnostic here stands for driver removal:
	 * intel_gt_driver_remove -> intel_gt_suspend_late -> gt_sanitize(false),
	 * i.e. stop the command streamers and reset the engines, BEFORE the
	 * objects they point at (status pages, contexts) are released.
	 */
	if (gteng_resumed) {
		static const int fwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
			OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
		unsigned ti, held = 0u;
		int frc = 0;

		while (held < 5u && (frc = osdep_fw_get(&mmio, fwd[held])) == 0)
			held++;
		if (held == 5u) {
			for (ti = 0u; ti < gteng.n; ti++)
				parity_execlists_reset_prepare(&gteng.ge[ti], &mmio);
			frc = parity_gt_reset_all(&uncore_lock, &mmio, 2000u);
			kern_logf("i915: parity teardown: engines stopped and reset (rc=%d) before release\n", frc);
		} else {
			kern_logf("i915: parity teardown: forcewake for the remove reset failed rc=%d\n", frc);
		}
		/* Put back only what was taken, in reverse. */
		while (held-- > 0u)
			osdep_fw_put(&mmio, fwd[held]);
	}
	if (eutest_inited)
		parity_eu_test_release(&eutest, &gtmem);
	if (drawtest_inited)
		parity_draw_test_release(&drawtest, &gtmem);
	if (r1test_inited)
		parity_r1_test_release(&r1test, &gtmem);
	if (textest_inited)
		parity_tex_test_release(&textest, &gtmem);
	if (t3test_inited)
		parity_t3_test_release(&t3test, &gtmem);
	if (gtmig_inited)
		parity_intel_migrate_fini(&gtmig, &gtmem);
	if (gtvwa_inited)
		parity_engines_verify_wa_release(&gtvwa, &gtmem);
	if (gtdef_inited) {
		unsigned di;

		for (di = 0u; di < gteng.n; di++)
			gteng.ge[di].default_state = 0;
		parity_engines_defaults_release(&gtdef, &gtmem);
	}
	if (gteng_inited && parity_lcd_kernel_gpu_retained()) {
		kern_logf("i915: parity teardown: engines NOT released (the GPU was not shown to be done with a kept buffer)\n");
	} else if (gteng_inited) {
		parity_intel_engines_release(&gteng, &gtmem);
		kern_logf("i915: parity teardown: engines released (status pages, kernel contexts)\n");
	}
	if (gtmem_inited && parity_lcd_kernel_gpu_retained()) {
		kern_logf("i915: parity teardown: the GPU was not shown to be done with a kept buffer -- the vm, its tables "
			"and every GT object stay (resources_retained=1)\n");
	} else if (gtmem_inited) {
		parity_gt_ppgtt_destroy(&gtmem, &gtpp);
		parity_gt_mem_fini(&gtmem);
		kern_logf("i915: parity teardown: GT objects released, GGTT window back to scratch "
			"(pte_writes=%u live=%u)\n", gtmem.pte_writes, gtmem.objects_live);
	}
	if (nogem_inited) {
		/* encoder destroy: intel_pps_vdd_off_sync() + the power layer's flush, before the records go */
		parity_edp_device_fini(&edp_dev);
		parity_intel_display_nogem_fini(&nogem);
		kern_logf("i915: parity teardown: display-nogem fini (crtc/plane/dpll records released)\n");
	}
	if (irq_installed) {
		if (pwc.irq_sync_failed) {
			kern_logf("i915: parity teardown: intel_irq_uninstall NOT run: a pipe interrupt drain failed earlier "
				"(irq_attached=1 resources_retained=1; irq_count=%u handled=%u none=%u)\n",
				irqdev.irq_count, irqdev.irq_handled_count, irqdev.irq_none_count);
		} else {
			parity_intel_irq_uninstall(&irqdev);
			kern_logf("i915: parity teardown: intel_irq_uninstall (sources reset, "
				"handler detached; irq_count=%u handled=%u none=%u)\n",
				irqdev.irq_count, irqdev.irq_handled_count, irqdev.irq_none_count);
		}
	}
	if (dstate_inited) {
		parity_intel_display_state_fini(&dstate);
		kern_logf("i915: parity teardown: display-state fini (global objs released, fbc freed)\n");
	}
	/* Reverse order.  DMC is torn down FIRST: sync the worker (flush, not
	 * cancel), process its reference, free the payload arena; then the workqueues. */
	if (dmc_inited) {
		parity_intel_dmc_fini(&dmc_dev, sched_ticks() + 500u);
		kern_logf("i915: parity teardown: intel_dmc_fini (worker synced; load_seq_completed=%d, payload_writes=%u, dmc_ref_held=%d)\n",
			dmc_dev.load_seq_completed_flag, dmc_dev.dmc.payload_writes, dmc_dev.dmc_wakeref_held);
		kern_logf("i915: parity teardown: DMC ver=%u.%u ids MAIN/A/B/C/D present=%d%d%d%d%d payload=%u/%u/%u/%u/%u\n",
			(unsigned)(dmc_dev.dmc.version>>16), (unsigned)(dmc_dev.dmc.version&0xffff),
			dmc_dev.dmc.dmc_info[0].present, dmc_dev.dmc.dmc_info[1].present, dmc_dev.dmc.dmc_info[2].present,
			dmc_dev.dmc.dmc_info[3].present, dmc_dev.dmc.dmc_info[4].present,
			dmc_dev.dmc.dmc_info[0].dmc_fw_size, dmc_dev.dmc.dmc_info[1].dmc_fw_size, dmc_dev.dmc.dmc_info[2].dmc_fw_size,
			dmc_dev.dmc.dmc_info[3].dmc_fw_size, dmc_dev.dmc.dmc_info[4].dmc_fw_size);
		parity_kworkqueue_destroy(&dmc_wq);
	}
	if (modeset_wq_ok) parity_kworkqueue_destroy(&modeset_wq);
	if (flip_wq_ok) parity_kworkqueue_destroy(&flip_wq);
	if (display_core_inited) {
		parity_intel_power_domains_driver_remove(&dcore);
		kern_logf("i915: parity teardown: display-core driver_remove (init rpm wakeref cancelled, wells kept)\n");
	}
	if (power_domains_inited) {
		parity_intel_power_domains_cleanup(&power_domains);
		kern_logf("i915: parity teardown: power domains map released\n");
	}
	/* intel_bios_driver_remove(): the VBT parser's lists and arena. */
	parity_intel_bios_driver_remove(&vbt_state);
	if (vga_registered) {
		parity_intel_vga_unregister(&vga_client);
		kern_logf("i915: parity teardown: VGA arbiter client unregistered\n");
	}
	if (drm_inited) {
		parity_drm_dev_fini(&drm_dev);
		kern_logf("i915: parity teardown: DRM device + vblank released\n");
	}
	if (msi_kept && osdep_pci_msi_enabled(&pci)) {
		osdep_pci_teardown_msi(&pci);
		kern_logf("i915: parity teardown: MSI resource released\n");
	}
	if (wc_aperture_va != 0) {
		int wc_urc = hal_space_unmap_device(wc_aperture_va, (size_t)wc_aperture_size);
		kern_logf("i915: parity teardown: WC aperture unmap va=%p size=0x%llx rc=%d\n",
			wc_aperture_va, (unsigned long long)wc_aperture_size, wc_urc);
	}
	if (parity_lcd_kernel_abandoned() || pwc.irq_sync_failed) {
		/* a scanout buffer may still be read by the display: its pages, the scratch page behind its guard PTEs, the DMA
		 * device (IOMMU mappings), the GGTT and bus mastering all stay as they are -- a leak is the safe side */
		kern_logf("i915: parity teardown: a scanout buffer was ABANDONED -- DMA device, scratch page, BARs and bus mastering are kept\n");
		goto lcdb_kept;
	}
	if (scratch_created) {
		/* Release the scratch CPU mapping / DMA mapping / backing pages together. */
		(void)drv_dma_vector_free(scratch_vec);
	}
	if (dma_created) {
		(void)drv_dma_device_destroy(dma39);
		device->dma = 0;
	}
	if (gtt_mapped)
		drv_pci_device_unmap_bar(device->pci, &device->gtt);
	if (bar_mapped)
		drv_pci_device_unmap_bar(device->pci, &device->regs);
	if (bar_claimed)
		drv_pci_device_release_bar(device->pci, GEN4_GTTMMADR_BAR);
	if (pci_enabled)
		osdep_pci_restore(&pci);
	if (probe_pm_held) {
		/* Release the PCI-core probe runtime PM reference last -- after all MMIO
		 * use and PCI teardown above have completed (async/MMIO done). */
		osdep_rpm_put(&probe_pm);
		kern_logf("i915: parity teardown: PCI probe runtime PM released (usage=%d)\n",
			osdep_rpm_usage(&probe_pm));
	}

lcdb_kept:
	parity_dump_trace(&trace);
	kern_logf("i915: parity attach end: reached=P%d outcome=%s where=%s err=%d\n",
		(int)res.reached - 1, outcome_name(res.outcome), res.where, res.error);

	if (out != 0)
		*out = res;
	return 0;
}
