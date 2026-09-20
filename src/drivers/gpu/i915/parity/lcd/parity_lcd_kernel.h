/*
 * WS031 Linux-parity -- LCD-B on the real GPU (parity_lcd_kernel.c): one known picture on the panel, a finite
 * observation window, the reference's stop path, everything given back.  Build: -DPARITY_LCDB_TEST=1.
 * zedBSD project code.
 */
#ifndef PARITY_LCD_KERNEL_H
#define PARITY_LCD_KERNEL_H

#include <stdint.h>

struct parity_edp_device;
struct osdep_mmio;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_display_core;
struct parity_cdclk_dev;
struct parity_display_nogem;
struct parity_display_state;
struct parity_bw_state;
struct parity_dmc_dev;
struct parity_irq_dev;
struct parity_gt_mem;
struct parity_gt_engines;
struct parity_gt_ppgtt;
struct spinlock;

#define PARITY_LCDB_PATTERN_ID  110u                     /* the asymmetric LCD-B picture (lcd_pattern.c) */
#define PARITY_LCDB_PATTERN_FNV 0xce63f20b23f91f85ull    /* its pinned hash at 1920x1080 (lcd-pattern-host.c) */
#ifndef PARITY_LCDB_WINDOW_MS
#define PARITY_LCDB_WINDOW_MS   20000u                   /* finite: long enough for the camera, then the stop path runs */
#endif

/* the objects of the normal initialisation this run reads and uses; none is copied, none is re-created */
struct parity_lcd_kernel_deps {
	struct parity_edp_device *edp;          /* resident panel: DPCD / EDID / LCD-A state, PPS, AUX, tick sleeps, VBT */
	struct osdep_mmio *mmio;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *pwc;
	struct parity_display_core *dcore;      /* DBUF slices */
	struct parity_cdclk_dev *cdclk;
	struct parity_display_nogem *nogem;     /* watermark latencies, SAGV block time */
	struct parity_display_state *dstate;    /* the bandwidth object: QGV mask */
	struct parity_bw_state *bw;             /* QGV / PSF bandwidth table */
	struct parity_dmc_dev *dmc;
	struct parity_irq_dev *irq;
	struct parity_gt_mem *gm;
	int ipc_enabled;                        /* skl_watermark_ipc_init()'s result */
	/* LCD-G only: the GT the draw is submitted to (forcewake is held by the caller) */
	struct parity_gt_engines *es;
	struct parity_gt_ppgtt *vm;
	struct spinlock *uncore_lock;
};

/* 0 = PASS (software state + hardware observation; the photograph is judged outside) */
int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d);
/* HDMI-B (-DPARITY_HDMI_B_TEST=1): one picture on the external HDMI display (DDI B, pipe B), then the stop path */
#ifndef PARITY_HDMIB_WINDOW_MS
#define PARITY_HDMIB_WINDOW_MS  20000u
#endif
int parity_lcd_kernel_hdmib_run(const struct parity_lcd_kernel_deps *d);
/* DUAL (-DPARITY_DUAL_TEST=1): the panel and the external HDMI display at once, each with its own picture */
#ifndef PARITY_DUAL_WINDOW_MS
#define PARITY_DUAL_WINDOW_MS   20000u
#endif
int parity_lcd_kernel_dual_run(const struct parity_lcd_kernel_deps *d);
/* DUAL-SHARED (-DPARITY_DUAL_SHARE_TEST=1): ONE buffer on both screens (the external one sees part of it) */
int parity_lcd_kernel_dual_share_run(const struct parity_lcd_kernel_deps *d);
/* LCD reuse (-DPARITY_LCDR_TEST=1): three show / stop cycles, pipe IRQ + vblank, brightness, backlight off / on */
int parity_lcd_kernel_lcdr_run(const struct parity_lcd_kernel_deps *d);
/* LCD-G (-DPARITY_LCDG_TEST=1): a GPU-drawn full-HD image shown from the same backing */
int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d);
/* LCD-C (-DPARITY_LCDC_TEST=1): one modeset, synchronous flips A -> B -> A -> B -> A, stop, both released */
int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d);
/* LCD-O (-DPARITY_LCDO_TEST=1): synthetic ASLE brightness requests (OpRegion service on a SHADOW mailbox) on the real LCD */
int parity_lcd_kernel_lcdo_run(const struct parity_lcd_kernel_deps *d);
/* LCD-D (-DPARITY_LCDD_TEST=1): the GPU redraws the hidden buffer (A/B at their own PPGTT VAs, mapped for the run), then the flip; 8 rounds */
int parity_lcd_kernel_lcdd_run(const struct parity_lcd_kernel_deps *d);
/* the release decision after a draw and (maybe) a show -- exported for the GPU-free test */
struct parity_fhd_render;
struct parity_scanout;
struct parity_gt_tlb;
struct parity_gt_engines;
int parity_lcdg_finish(struct parity_fhd_render *fr, struct parity_scanout *so, int display_acquired, int display_released,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb, struct parity_gt_engines *es,
	struct osdep_mmio *m, struct spinlock *uncore_lock, int *render_rc);
/* 1 while the GPU is not shown to be done with retained objects: the probe skips the GT teardown too */
int parity_lcd_kernel_gpu_retained(void);
/* 1 while a run's resources are retained (the display may still read): the outer teardown keeps DMA, bus mastering, scratch */
int parity_lcd_kernel_abandoned(void);

/* what the runner reports next to the probe result (the probe outcome itself is not changed by the LCD test) */
struct parity_lcd_test_summary {
	int ran, pass;
	const char *stage;                      /* furthest stage */
	const char *first_anomaly, *first_anomaly_stage;
	int cleanup_rc;                         /* the disable commit's result */
	unsigned cleanup_errors;
	int retained;                           /* resources kept because the stop was not confirmed */
};
void parity_lcd_kernel_summary(struct parity_lcd_test_summary *out);

#endif /* PARITY_LCD_KERNEL_H */
