/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Type and constant definitions derived from the Linux kernel (drivers/gpu/drm/i915/display/intel_display_limits.h),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 */

/*
 * Type and constant definitions derived from the Linux kernel (drivers/gpu/drm/i915/display/intel_global_state.h),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

/*
 * The display part's own types: what every display file shares.
 *
 * This header is private to src/drivers/gpu/i915/display/.  It holds three
 * things, in this order:
 *
 *   the Linux base      the few integer types and register helpers whose
 *                       meaning was identical in every display translation
 *                       unit of the old tree, so that one definition serves
 *                       all of them;
 *   the display state   the zedBSD types the display files pass to each
 *                       other (power wells, clocks, the panel, the VBT, the
 *                       scanout, the resident present path);
 *   the display root    struct i915_display, which the device owns and which
 *                       holds every long-lived display object.
 *
 * The Linux display code is compiled in one of four Linux environments,
 * each a private header of its own:
 *
 *   modeset-internal.h   the modeset, DDI, DPLL, plane, colour, backlight and
 *                        vblank text (with watermark-internal.h and
 *                        takeover-internal.h layered on it);
 *   dp-internal.h        the DP AUX, panel power sequencer, DP helper and
 *                        EDID text (on vbt.h);
 *   vbt.h                the VBT parser's environment;
 *   hotplug-internal.h   the hotplug, GMBUS and HDMI detect text;
 *
 * The same Linux type name (struct drm_i915_private, struct intel_dp,
 * struct drm_display_mode and others) has a different layout in each
 * environment, and several Linux helper names (intel_de_read, mutex_lock,
 * the connector and crtc iterators) mean something different in each.  A
 * translation unit therefore includes at most one environment: each one
 * records itself in I915_DISPLAY_LINUX_WORLD and refuses to be included
 * next to another.  A helper name whose meaning differed between
 * environments is not defined under its Linux name at all; every meaning
 * has an explicit name of its own in the environment it belongs to.
 *
 * Nothing in this header depends on an environment, so every display file
 * includes it first, and a file that uses no Linux text includes nothing
 * else of the display part.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_INTERNAL_H
#define DRIVERS_GPU_I915_DISPLAY_INTERNAL_H

/*
 * The xd display files are compiled with I915_XD_STRICT by their check, so
 * a path by which one of them reaches this header stops the compile.
 */
#ifdef I915_XD_STRICT
#error "an xd display file includes internal.h, the older display code's header"
#endif

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kern/kcrt.h>
#include <kern/lock.h>
#include <kern/waitq.h>

#include "../sync.h"
#include "../trace.h"
#include "../workqueue.h"
#include "display-internal.h"
#include "dp-ext.h"
#include "tc.h"
#include "xd.h"
#include "xd-backend.h"
#include "xd-edp.h"

/*
 * The old name of struct i915_display_mode (display-internal.h).
 *
 * The display 12 and 13 code that the xd rewrite replaces, and the tests
 * of other workstreams written against it, still spell the mode type by
 * this name.  The name goes with that code.
 */
#define i915_lcd_mode i915_display_mode

/*
 * The Linux base: integer types.
 *
 * u64 and s64 were unsigned long long and long long in the hotplug
 * translation units and uint64_t and int64_t everywhere else; both are 64
 * bits wide, so only the spelling of the type (and the printf length
 * modifier a caller needs) differs.  The uint64_t spelling is kept.
 */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/*
 * One display register, as the Linux register definitions name it.
 *
 * Wrapping the offset in a structure keeps a register from being passed
 * where a value is expected; every Linux register macro of the intel/ headers
 * builds one with _MMIO().
 */
typedef struct {
	/* The register's offset in the MMIO BAR. */
	u32 reg;
} i915_reg_t;

/* Builds the register value of a Linux register macro. */
#define _MMIO(r) ((const i915_reg_t){ .reg = (r) })

/* The register value that names no register. */
#define INVALID_MMIO_REG _MMIO(0)

/* Bit n of a 32-bit register. */
#define REG_BIT(n) ((u32)1u << (n))

/* The contiguous bits h down to l of a 32-bit register. */
#define REG_GENMASK(h, l) ((u32)((0xffffffffu >> (31 - (h))) & (0xffffffffu << (l))))

/*
 * Places a field value into its mask, and takes it out again.
 *
 * The modeset translation units divided by the mask's lowest bit where the
 * DP ones shifted by its position; for a nonzero 32-bit mask both give the
 * same value, so the shift is kept.
 */
#define REG_FIELD_PREP(mask, val) ((u32)((((u32)(val)) << __builtin_ctz(mask)) & (mask)))
#define REG_FIELD_GET(mask, val) ((u32)((((u32)(val)) & (mask)) >> __builtin_ctz(mask)))

/* Bit n of a mask that is not a register (the Linux BIT()). */
#define BIT(n) (1u << (n))

/* Picks the register of instance index from the first two, evenly spaced. */
#define _PICK_EVEN(index, a, b) ((a) + (index) * ((b) - (a)))

/*
 * Picks from two evenly spaced ranges, the second starting at c_index.
 *
 * The modeset translation units added the Linux BUILD_BUG_ON_ZERO() check,
 * which was defined to 0 there; the value is the same without it.
 */
#define _PICK_EVEN_2RANGES(index, c_index, a, b, c, d) \
	((index) < (c_index) ? _PICK_EVEN(index, a, b) : _PICK_EVEN((index) - (c_index), c, d))

/*
 * The register of a port, from the registers of ports A and B.
 *
 * The DP translation units spelled this through _PORT(), which is
 * _PICK_EVEN() under another name.
 */
#define _MMIO_PORT(port, a, b) _MMIO(_PICK_EVEN(port, a, b))

/* The number of elements of an array. */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* n divided by d, rounded up. */
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* The structure a member pointer points into. */
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))

/*
 * The smaller and the larger of two values, each evaluated once.
 *
 * The hotplug translation units used a form that evaluated its arguments
 * twice.  Their only uses are the GMBUS transfer lengths, where the second
 * argument is a constant or gmbus_max_xfer_size(), which only reads the
 * display version; evaluating it once does not change the result.
 */
#define min(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#define min_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a < _b ? _a : _b; })
#define max_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a > _b ? _a : _b; })

/* The offset of a register value. */
static __inline u32
i915_mmio_reg_offset(
	i915_reg_t reg)
{
	/* Reports the offset the register value carries. */
	return reg.reg;
}

/* Tells whether a register value names a register. */
static __inline bool
i915_mmio_reg_valid(
	i915_reg_t reg)
{
	/* Offset 0 is INVALID_MMIO_REG. */
	if (reg.reg == 0U)
		return false;

	/* Succeeded: the value names a register. */
	return true;
}

/* Tells whether two register values name the same register. */
static __inline bool
i915_mmio_reg_equal(
	i915_reg_t a,
	i915_reg_t b)
{
	/* Registers are the same when their offsets are. */
	if (a.reg != b.reg)
		return false;

	/* Succeeded: both name one register. */
	return true;
}

/*
 * The Linux environment protocol.
 *
 * Each environment header starts with
 *
 *	#ifdef I915_DISPLAY_LINUX_WORLD
 *	#error "..."
 *	#endif
 *	#define I915_DISPLAY_LINUX_WORLD "<its name>"
 *
 * and defines I915_DISPLAY_WORLD_<NAME> for the headers layered on it
 * (watermark-internal.h and takeover-internal.h require
 * I915_DISPLAY_WORLD_MODESET, dp-internal.h requires I915_DISPLAY_WORLD_VBT
 * and replaces the world name with "dp").
 */

/*
 * The power wells and domains (power.c).
 */
/*
 * display power domains (structure + map), see power.c.
 *
 * The domains are the older display code's names for what it powers.
 * Each one stands for a set of functions of the xd power wells, whose
 * counts decide which wells are up; power_wells[] is a copy of the
 * platform's well table (its Type-C cold mailbox well left out) that the
 * older code reads: the names, the operations family, the users of each
 * well and the last known state.
 *
 * DC state values are the register-bit encodings (not abstract flags).
 */

struct i915_trace;

/* enum intel_display_power_domain (v6.8 order preserved). */
enum i915_power_domain {
	I915_PW_DOMAIN_DISPLAY_CORE = 0,
	I915_PW_DOMAIN_PIPE_A, I915_PW_DOMAIN_PIPE_B,
	I915_PW_DOMAIN_PIPE_C, I915_PW_DOMAIN_PIPE_D,
	I915_PW_DOMAIN_PIPE_PANEL_FITTER_A, I915_PW_DOMAIN_PIPE_PANEL_FITTER_B,
	I915_PW_DOMAIN_PIPE_PANEL_FITTER_C, I915_PW_DOMAIN_PIPE_PANEL_FITTER_D,
	I915_PW_DOMAIN_TRANSCODER_A, I915_PW_DOMAIN_TRANSCODER_B,
	I915_PW_DOMAIN_TRANSCODER_C, I915_PW_DOMAIN_TRANSCODER_D,
	I915_PW_DOMAIN_TRANSCODER_EDP, I915_PW_DOMAIN_TRANSCODER_DSI_A,
	I915_PW_DOMAIN_TRANSCODER_DSI_C, I915_PW_DOMAIN_TRANSCODER_VDSC_PW2,
	I915_PW_DOMAIN_PORT_DDI_LANES_A, I915_PW_DOMAIN_PORT_DDI_LANES_B,
	I915_PW_DOMAIN_PORT_DDI_LANES_C, I915_PW_DOMAIN_PORT_DDI_LANES_D,
	I915_PW_DOMAIN_PORT_DDI_LANES_E, I915_PW_DOMAIN_PORT_DDI_LANES_F,
	I915_PW_DOMAIN_PORT_DDI_LANES_TC1, I915_PW_DOMAIN_PORT_DDI_LANES_TC2,
	I915_PW_DOMAIN_PORT_DDI_LANES_TC3, I915_PW_DOMAIN_PORT_DDI_LANES_TC4,
	I915_PW_DOMAIN_PORT_DDI_LANES_TC5, I915_PW_DOMAIN_PORT_DDI_LANES_TC6,
	I915_PW_DOMAIN_PORT_DDI_IO_A, I915_PW_DOMAIN_PORT_DDI_IO_B,
	I915_PW_DOMAIN_PORT_DDI_IO_C, I915_PW_DOMAIN_PORT_DDI_IO_D,
	I915_PW_DOMAIN_PORT_DDI_IO_E, I915_PW_DOMAIN_PORT_DDI_IO_F,
	I915_PW_DOMAIN_PORT_DDI_IO_TC1, I915_PW_DOMAIN_PORT_DDI_IO_TC2,
	I915_PW_DOMAIN_PORT_DDI_IO_TC3, I915_PW_DOMAIN_PORT_DDI_IO_TC4,
	I915_PW_DOMAIN_PORT_DDI_IO_TC5, I915_PW_DOMAIN_PORT_DDI_IO_TC6,
	I915_PW_DOMAIN_PORT_DSI, I915_PW_DOMAIN_PORT_CRT,
	I915_PW_DOMAIN_PORT_OTHER, I915_PW_DOMAIN_VGA,
	I915_PW_DOMAIN_AUDIO_MMIO, I915_PW_DOMAIN_AUDIO_PLAYBACK,
	I915_PW_DOMAIN_AUX_IO_A, I915_PW_DOMAIN_AUX_IO_B, I915_PW_DOMAIN_AUX_IO_C,
	I915_PW_DOMAIN_AUX_IO_D, I915_PW_DOMAIN_AUX_IO_E, I915_PW_DOMAIN_AUX_IO_F,
	I915_PW_DOMAIN_AUX_A, I915_PW_DOMAIN_AUX_B, I915_PW_DOMAIN_AUX_C,
	I915_PW_DOMAIN_AUX_D, I915_PW_DOMAIN_AUX_E, I915_PW_DOMAIN_AUX_F,
	I915_PW_DOMAIN_AUX_USBC1, I915_PW_DOMAIN_AUX_USBC2, I915_PW_DOMAIN_AUX_USBC3,
	I915_PW_DOMAIN_AUX_USBC4, I915_PW_DOMAIN_AUX_USBC5, I915_PW_DOMAIN_AUX_USBC6,
	I915_PW_DOMAIN_AUX_TBT1, I915_PW_DOMAIN_AUX_TBT2, I915_PW_DOMAIN_AUX_TBT3,
	I915_PW_DOMAIN_AUX_TBT4, I915_PW_DOMAIN_AUX_TBT5, I915_PW_DOMAIN_AUX_TBT6,
	I915_PW_DOMAIN_GMBUS, I915_PW_DOMAIN_GT_IRQ,
	I915_PW_DOMAIN_DC_OFF, I915_PW_DOMAIN_TC_COLD_OFF,
	I915_PW_DOMAIN_INIT,
	I915_PW_DOMAIN_NUM
};

/*
 * The AUX channel numbers of display versions 12 and 13 (enum aux_ch): the
 * combo channels A to C, then the Type-C channels USBC1 on from AUX_CH_D.
 * Display version 13 reuses USBC5 and USBC6 for its combo ports D and E.
 */
#define I915_AUX_CH_A		0
#define I915_AUX_CH_C		2
#define I915_AUX_CH_USBC1	3
#define I915_AUX_CH_USBC4	6
#define I915_AUX_CH_USBC6	8
#define I915_AUX_CH_D_XELPD	7
#define I915_AUX_CH_E_XELPD	8

enum i915_power_domain drv_i915_aux_legacy_power_domain(unsigned display_ver, int aux_ch);
enum i915_power_domain drv_i915_aux_tbt_power_domain(unsigned display_ver, int aux_ch);
enum i915_power_domain drv_i915_aux_io_power_domain(unsigned display_ver, int aux_ch);

/* DC state flags -- reference register-bit encodings (intel_dmc_regs / power). */
#define I915_DC_STATE_EN_UPTO_DC5   0x00000001u
#define I915_DC_STATE_EN_UPTO_DC6   0x00000002u
#define I915_DC_STATE_EN_DC9        0x00000008u
#define I915_DC_STATE_EN_DC3CO      0x40000000u

/* i915_power_well_id values (DISP_PW_ID_NONE must be 0). */
#define I915_DISP_PW_ID_NONE   0
#define I915_SKL_DISP_PW_1     8
#define I915_SKL_DISP_PW_2     9
#define I915_ICL_DISP_PW_3     10
#define I915_SKL_DISP_DC_OFF   11

/* Which ops family a well uses (bodies executed at init_hw). */
enum i915_pw_ops_kind {
	I915_PW_OPS_ALWAYS_ON = 0,
	I915_PW_OPS_HSW,        /* hsw_power_well_ops */
	I915_PW_OPS_ICL_DDI,    /* icl_ddi_power_well_ops */
	I915_PW_OPS_ICL_AUX,    /* icl_aux_power_well_ops */
	I915_PW_OPS_DC_OFF,     /* gen9_dc_off_power_well_ops */
};

/* A runtime power well (i915_power_well: desc data + separate live state). */
struct i915_power_well {
	const char *name;
	enum i915_pw_ops_kind ops;
	int always_on;
	int has_vga;
	int has_fuses;
	int is_tc_tbt;
	int fixed_enable_delay;
	unsigned enable_timeout;     /* ms; 0 = ops default */
	unsigned irq_pipe_mask;
	unsigned hsw_idx;            /* HSW/ICL power-well control index (reference) */
	int id;                     /* i915_power_well_id (0 = DISP_PW_ID_NONE) */
	unsigned xd_well;           /* the well's entry in the platform's table */
	/* Live state, copied from the xd power well counts and records: */
	unsigned refcount;          /* the well's users */
	int hw_enabled;             /* last known HW state (-1 = unknown until sync) */
};

#define I915_PW_MAX  40

/* i915_power_domains: the whole power-domain state for the device. */
struct i915_power_domains {
	uint32_t allowed_dc_mask;
	uint32_t target_dc_state;
	int disable_power_well;      /* sanitized option */
	struct mutex lock;
	int async_put_work_inited;   /* INIT_DELAYED_WORK(async_put_work) */

	struct i915_power_well power_wells[I915_PW_MAX];
	unsigned num_power_wells;
	/* per-domain -> bitmask of well indices that provide it. */
	uint64_t domain_wells[I915_PW_DOMAIN_NUM];

	/* The xd state whose power well counts the domains use, and the display version. */
	struct i915_xd *xd;
	unsigned display_ver;

	int map_initialized;
	int initialized;

	/* domain_use_count[]: references per DOMAIN (the wells count separately) */
	unsigned domain_use_count[I915_PW_DOMAIN_NUM];

	/*
	 * The delayed work behind a last reference put asynchronously: the
	 * wells stay up, owed to the work, until it runs or a get takes them
	 * over (the xd power well counts keep what is owed).  The counters
	 * are copies of the counts' diagnostics, kept for the logs.
	 */
	const struct i915_pw_async_ops *async_ops;   /* NULL: put_async puts at once */
	void *async_ctx;
	struct i915_pw_ctx *async_pwc;              /* the context the delayed work uses */
	unsigned async_puts, async_parked, async_grabs, async_work_runs, async_work_empty;
	unsigned async_released, async_requeues, async_flushes, async_state_errors, use_count_errors;
};

/* the delayed work behind put_async; supplied by the embedder (real timer + worker, or a test) */
struct i915_pw_async_ops {
	int (*queue)(void *ctx, int delay_ms);    /* queue_delayed_work(): 1 newly queued */
	int (*cancel)(void *ctx, int sync);       /* cancel_delayed_work[_sync]() */
};

struct i915_mmio;
struct i915_vga_client;
struct i915_cdclk_dev;

/*
 * Context for the power-well operation bodies: the MMIO handle, the VGA client
 * (has_vga post-enable) and the IRQ-enabled gate (intel_irqs_enabled(); 0 before
 * the interrupts are set up).  The *_calls counters are diagnostics the GPU-free tests observe.
 */
struct i915_xd_dkl;
struct i915_pw_ctx {
	struct i915_mmio *mmio;
	struct i915_vga_client *vga;
	int irqs_enabled;
	unsigned vga_reset_calls;
	unsigned irq_post_enable_calls;
	unsigned irq_pre_disable_calls;
	/*
	 * Set when a pipe's interrupt drain failed before its well would have gone off.  From then on every well disable
	 * is refused (the handler that did not finish may still touch display registers), the well that was about to go
	 * off stays owned, and the probe's teardown keeps the IRQ handler attached and the device resources in place.
	 */
	int irq_sync_failed;
	unsigned disable_refusals;             /* well disables refused because of it */
	unsigned kept_wells;                   /* wells kept on by a refused disable */
	const struct i915_pw_irq_ops *irq_ops;   /* NULL: the IRQ layer is not bound (GPU-free tests) */
	void *irq_ctx;
	unsigned ack_timeouts;   /* real HW ACK timeouts (warn+continue) */
	/*
	 * DC_off power-well enable (gen9_disable_dc_states) needs the shared CDCLK,
	 * the saved DBUF slice mask and the target DC state -- these are the SAME
	 * device objects the parent uses (not per-well copies).
	 */
	struct i915_cdclk_dev *cd;
	uint8_t *dbuf_slices;          /* saved DBUF mask, for gen9_assert_dbuf_enabled */
	uint32_t target_dc_state;
	uint32_t allowed_dc_mask;
	unsigned dc_off_enable_calls;  /* diagnostics observed by the GPU-free tests */
	unsigned dc_off_cdclk_readouts;
	unsigned dc_off_dbuf_asserts;
	unsigned dc_off_combo_inits;
	/*
	 * DC_off power-well DISABLE (gen9_dc_off_power_well_disable): enables the
	 * target DC state, but only once the DMC firmware payload is loaded.
	 */
	int display_ver;               /* gen9_dc_mask() */
	int dmc_has_payload;           /* intel_dmc_has_payload() */
	uint32_t dc_state;             /* display.dmc.dc_state: what we last wrote */
	unsigned dc_off_disable_calls;
	unsigned dc_state_writes;
	unsigned dc_state_rewrites;
	/*
	 * The display's Dekel PHY windows, which a Type-C AUX well's enable reads
	 * the PHY microcontroller's health through; NULL (the GPU-free tests)
	 * skips that read.  tc_uc_health_timeouts counts the enables whose PHY did not
	 * report itself healthy in time.
	 */
	struct i915_xd_dkl *dkl;
	unsigned tc_uc_health_timeouts;
	/*
	 * The display's xd state: its register window and the platform's
	 * description, whose power wells the well operations switch.
	 */
	struct i915_xd *xd;
};

/* pmdemand state: mutex + waitqueue, initialised early. */
struct i915_pmdemand {
	struct mutex lock;
	struct wait_queue waitqueue;
	int early_initialized;
};
/*
 * The CDCLK (clock.c).
 */
/*
 * CDCLK init (see clock.c).
 *
 * Faithful port of the ADL-P CDCLK bring-up: intel_init_cdclk_hooks() selects
 * the platform table + callbacks (ADL-P D0 -> adlp_cdclk_table + tgl funcs);
 * intel_cdclk_init_hw() -> bxt_cdclk_init_hw() reads the pre-OS state
 * (bxt_get_cdclk via the DE PLL / CDCLK_CTL), sanitises it (bxt_sanitize_cdclk),
 * and only if the state is not a legal freq/VCO for this platform reprograms it
 * (bxt_set_cdclk, using the common PCODE mailbox).  This is intel_cdclk_init_hw,
 * NOT intel_cdclk_init (which reprograms to the min cdclk later in the start).
 */

struct mutex;

/* Display stepping ordinals (subset of enum intel_step; ordering is faithful). */
enum i915_display_step {
	I915_STEP_NONE = 0,
	I915_STEP_A0, I915_STEP_A1, I915_STEP_A2, I915_STEP_A3,
	I915_STEP_B0, I915_STEP_B1, I915_STEP_B2, I915_STEP_B3,
	I915_STEP_C0, I915_STEP_C1, I915_STEP_C2, I915_STEP_C3,
	I915_STEP_D0
};

/*
 * intel_cdclk_config: dev_priv->display.cdclk.hw plus computed targets.
 * vco special values: 0 = PLL off (bypass), ~0u = unknown (sanitize wants a
 * full PLL disable+enable).  Never treat ~0u as a real frequency.
 */
struct i915_cdclk_config {
	uint32_t ref;      /* refclk kHz */
	uint32_t vco;      /* PLL VCO kHz; 0 = off; ~0u = unknown/force */
	uint32_t cdclk;    /* cdclk kHz; 0 = force reprogram */
	uint32_t bypass;   /* bypass kHz */
	uint8_t  voltage_level;
};

/*
 * The CDCLK of the display as the older code reads it: the state the
 * hardware is in (a copy of what the xd clock read or set), the xd state
 * the CDCLK is read and set through, the platform's facts, and the
 * diagnostics of the last change.
 */
struct i915_cdclk_dev {
	struct i915_cdclk_config hw;          /* current HW state (display.cdclk.hw) */
	struct i915_xd *xd;                   /* the xd window and platform description */
	int display_ver;                         /* 13 for ADL-P */
	int has_cdclk_crawl;                     /* ADL-P: 1 */
	int has_cdclk_squash;                    /* ADL-P: 0 */
	struct i915_mmio *m;                    /* MMIO backend */
	struct mutex *sb_lock;                   /* PCODE sideband lock */

	/* Diagnostics captured across init_hw (observed_before ... state_after). */
	struct i915_cdclk_config diag_observed_before;
	struct i915_cdclk_config diag_sanitized;
	struct i915_cdclk_config diag_requested;
	int diag_no_change;            /* 1 = sanitize accepted HW; nothing reprogrammed */
	int diag_prepare_status;       /* PCODE prepare-for-change result (0 / -errno) */
	int diag_hw_sequence_reached;  /* 1 once the PLL/CDCLK_CTL writes ran */
	int diag_notify_status;        /* PCODE voltage-notify result */
};

/*
 * The display core (power.c).
 */
/*
 * display-core HW bring-up (see power.c).
 *
 * Ports intel_power_domains_init_hw(false) -> icl_display_core_init(false) for
 * ADL-P: DC-state disable, PCH reset handshake, combo PHY init (with the void
 * contract's fault check), PW1 enable (fuses), CDCLK init, DBUF slice enable,
 * BW_BUDDY, the xe_lpd workarounds, then hold the POWER_DOMAIN_INIT reference
 * and sync every power well.  Children share ONE device state (same MMIO, the
 * same power-domains + cdclk + PCODE sb_lock); nothing is re-initialised per
 * child.  intel_power_domains_driver_remove() cancels the init wakeref's rpm
 * side while KEEPING the wells enabled (not a domain put).
 */

struct i915_power_domains;
struct i915_pw_ctx;

/* Shared device state threaded through the display-core bring-up. */
struct i915_display_core {
	struct i915_power_domains *pd;   /* the one power-domains state */
	struct i915_cdclk_dev *cd;       /* the one cdclk state (cd->m == m) */
	struct i915_pw_ctx *pwc;         /* power-well op context (pwc->mmio == m) */
	struct i915_mmio *m;              /* the one MMIO backend */
	struct mutex *sb_lock;             /* the one PCODE sideband lock */

	/* DRAM info read at the start (BW_BUDDY table lookup). */
	int dram_type;                     /* enum i915_dram_type */
	unsigned dram_channels;

	/* Live state (not re-initialised per child). */
	int initializing;
	uint8_t dbuf_enabled_slices;
	/* how many DBUF slices this display has (display version 13: 4, version 12: 2) */
	uint8_t dbuf_slice_mask;
	/*
	 * which MBUS ABOX registers this display has (intel_display_device.c .abox_mask);
	 * 0 = the platform programs none (Alder Lake-P and display 14+)
	 */
	uint8_t abox_mask;
	int display_ver;                /* 12 = Tiger Lake, 13 = Alder Lake-P class */
	/*
	 * the reference gates a few display steps on the platform, not on the version
	 * (gen12_dbuf_slices_config() returns immediately on Alder Lake-P alone)
	 */
	uint8_t is_alderlake_p;
	int init_wakeref_held;             /* POWER_DOMAIN_INIT domain reference held */
	int core_suspended;                /* the display core was taken down for a suspend */
	int pm_wakeref;                    /* runtime-PM side of the init wakeref */

	/* Diagnostics. */
	int fault_stop;                    /* 1 once an adaptation-layer fault stopped us */
	const char *fault_where;           /* child at which we stopped */
	int reached_init_ref;
	int reached_sync_hw;
	int last_child;                    /* ordinal of the last child entered */
};

/*
 * The DMC firmware (dmc.c).
 */
/*
 * DMC firmware parse (F1) + load (F2) + async lifecycle (F3).
 *
 * Faithful port of intel_dmc.c for ADL-P (display version 13).  This header
 * covers the parse: the firmware blob (from the read-only provider) is validated
 * (CSS / package / per-DMC headers), the entry matching the display stepping is
 * selected for each DMC id, and its payload is copied into DEVICE-OWNED storage
 * so it outlives the released firmware handle.  Length units differ per header
 * (CSS/package/v3 in dwords, v1 in bytes) and the entry offset is relative to the
 * end of the CSS+package, exactly as the reference.
 */

enum i915_dmc_id {
	I915_DMC_FW_MAIN = 0,
	I915_DMC_FW_PIPEA,
	I915_DMC_FW_PIPEB,
	I915_DMC_FW_PIPEC,
	I915_DMC_FW_PIPED,
	I915_DMC_FW_MAX
};

#define I915_DMC_MAX_MMIO 20   /* DMC_V3_MAX_MMIO_COUNT */

struct i915_dmc_info {
	int present;               /* an fw_info entry was selected for this id */
	uint32_t dmc_offset;       /* dwords (fw_info.offset), relative to CSS+package end */
	uint32_t mmio_count;
	uint32_t mmioaddr[I915_DMC_MAX_MMIO];
	uint32_t mmiodata[I915_DMC_MAX_MMIO];
	uint32_t start_mmioaddr;   /* program RAM start MMIO (v3) / DMC_V1_MMIO_START (v1) */
	uint32_t dmc_fw_size;      /* dwords (program size, excl. header) */
	const uint8_t *payload;    /* device-owned copy; NULL until saved */
	uint32_t payload_size;     /* bytes */
	int header_ver;            /* 1 or 3 */
};

/*
 * One parsed DMC firmware image: its header, the payload of each DMC id
 * and the registers the load writes.
 */
struct i915_dmc {
	uint32_t version;          /* CSS version (major<<16 | minor) */
	uint32_t max_fw_size;      /* per-platform ceiling (bytes) */
	int display_ver;           /* 13 for ADL-P */
	char stepping;             /* display stepping char, e.g. 'D' */
	char substepping;          /* e.g. '0' */
	struct i915_dmc_info dmc_info[I915_DMC_FW_MAX];
	/* Diagnostics captured by the parse (for the GPU-free tests). */
	uint32_t css_header_len_bytes;
	uint32_t package_header_ver;
	uint32_t num_entries;
	int truncated;             /* set on a refused (truncated/invalid) blob */
	/* F2 load diagnostics. */
	unsigned payload_writes;   /* DMC_PROGRAM DWORD writes performed */
	unsigned aux_writes;       /* trailing per-DMC MMIO writes */
	unsigned evt_disable_writes; /* event-handler CTL+HTP disable writes */
	int load_seq_completed;    /* 1 only after the WHOLE sequence (incl. post) ran */
	uint64_t psum;             /* order-sensitive checksum of payload (addr,val) */
	uint64_t asum;             /* order-sensitive checksum of aux (addr,transformed val) */
};

/*
 * DMC async lifecycle (F3).  intel_dmc_init takes the DMC's OWN POWER_DOMAIN_INIT
 * reference (distinct from the display-core parent's), prepares the state and
 * queues the load worker; the worker acquires the firmware, parses, loads and,
 * on success, releases the DMC reference; fini flushes the worker (flush, not
 * cancel), releases any still-held DMC reference, and frees the payload arena.
 */
struct i915_dmc_dev {
	struct i915_dmc dmc;
	struct i915_mmio *m;
	struct i915_power_domains *pd;
	struct i915_pw_ctx *pwc;
	struct i915_workqueue *wq;
	const char *fw_path;
	/* dmc_fallback_path() is a platform question, not a path question */
	int is_alderlake_p;
	struct i915_work work;
	int dmc_wakeref_held;          /* DMC's own INIT reference is held */
	uint32_t dc_state;
	/* diagnostics (kept distinct, per the reference). */
	int work_submitted;
	int worker_started;
	int firmware_acquired;
	int fallback_requested;
	int main_payload_present;      /* NOT the same as load_seq_completed */
	int load_seq_completed_flag;
	int first_fault;
};

/*
 * The DRAM and display bandwidth (watermark.c).
 */
/*
 * DRAM info + display bandwidth (hw_probe tail).
 *
 * Ports intel_dram_detect() and intel_bw_init_hw() -> tgl_get_bw_info(&adlp_sa_info)
 * for ADL-P, driven by the common PCODE mailbox.  The results are written into a
 * device-owned bandwidth state (i915_bw_state) that later stages read, mirroring
 * the reference i915->display.bw.max[] / sagv.status.  Both are void in the
 * reference: a tolerated PCODE/data failure is logged and the caller continues.
 */

#define I915_NUM_QGV_POINTS 8   /* I915_NUM_QGV_POINTS */
#define I915_NUM_PSF_POINTS 3   /* I915_NUM_PSF_GV_POINTS */
#define I915_BW_GROUPS      6   /* ARRAY_SIZE(display.bw.max) */

/* INTEL_DRAM_* subset reachable on ADL-P's gen12 decode. */
enum i915_dram_type {
	I915_DRAM_DDR4 = 0,
	I915_DRAM_DDR5,
	I915_DRAM_LPDDR5,
	I915_DRAM_LPDDR4,
	I915_DRAM_DDR3,
	I915_DRAM_LPDDR3,
	I915_DRAM_UNKNOWN
};

/* SAGV status (display.sagv.status subset). */
enum i915_sagv_status {
	I915_SAGV_UNKNOWN = 0,
	I915_SAGV_DISABLED,
	I915_SAGV_ENABLED,
	I915_SAGV_NOT_CONTROLLED
};

/* Subset of dram_info the bandwidth branch consumes. */
struct i915_dram_info {
	int type;                   /* enum i915_dram_type */
	unsigned num_channels;
	unsigned num_qgv_points;
	unsigned num_psf_gv_points;
	int wm_lv_0_adjust_needed;
	int valid;                  /* global info decoded successfully */
};

/* Mirrors struct intel_bw_info (display.bw.max[i]). */
struct i915_bw_group {
	unsigned deratedbw[I915_NUM_QGV_POINTS];
	unsigned peakbw[I915_NUM_QGV_POINTS];
	unsigned psf_bw[I915_NUM_PSF_POINTS];
	unsigned num_qgv_points;
	unsigned num_psf_gv_points;
	unsigned num_planes;
};

/* Device-owned display bandwidth state (display.bw / sagv.status). */
struct i915_bw_state {
	struct i915_bw_group max[I915_BW_GROUPS];
	int sagv_status;
	int valid;                  /* bandwidth table computed */
};
/*
 * The display software state (state.c, watermark.c).
 */
/*
 * tail: intel_mode_config_init() .. intel_fbc_init().
 *
 * Ports the remainder of intel_display_driver_probe_noirq() that follows the
 * (asynchronous) DMC load and the modeset/flip workqueues:
 *
 *   intel_mode_config_init(i915);          -- void
 *   intel_cdclk_init(i915);                -- global state object
 *   intel_color_init(i915);                -- DISPLAY_VER == 10 only
 *   intel_dbuf_init(i915);                 -- global state object
 *   intel_bw_init(i915);                   -- global state object + forced SAGV disable
 *   intel_pmdemand_init(i915);             -- global state object
 *   intel_init_quirks(i915);               -- void
 *   intel_fbc_init(i915);                  -- void
 *
 * Four of them register an intel_global_obj on display.global.obj_list, so the
 * shared mechanism (intel_atomic_global_obj_init) is ported once here and the
 * insertion ORDER is observable: cdclk, dbuf, bw, pmdemand.
 *
 * Note the two distinct "bw" states the reference keeps, which the port also
 * keeps apart:
 *   - display.bw.max[] / sagv.status  -- the PCODE-probed HW table from the start,
 *     already owned by struct i915_bw_state (internal.h).
 *   - struct intel_bw_state           -- the ATOMIC global state object created
 *     here, holding qgv_points_mask (i915_bw_obj_state below).
 *
 * The reference kzalloc()s each state object and propagates -ENOMEM; the port
 * uses device-owned storage behind an explicit allocator so those failure paths
 * stay reachable and testable.
 */

struct i915_bw_state;

/*
 * ---------------------------------------------------------------- *
 * intel_global_state.h: intel_global_obj / intel_global_state
 * ----------------------------------------------------------------
 */

struct i915_global_obj;

/*
 * One global atomic state object's state, as the Linux intel_global_state
 * carries it.
 */
struct i915_global_state {
	struct i915_global_obj *obj;
	unsigned ref;                 /* kref_init() -> 1 */
	int changed;
};

/*
 * The duplicate and destroy hooks of a global atomic state object.
 */
struct i915_global_state_funcs {
	struct i915_global_state *(*duplicate_state)(struct i915_global_obj *obj);
	void (*destroy_state)(struct i915_global_obj *obj,
		struct i915_global_state *state);
};

/*
 * A global atomic state object (the Linux intel_global_obj): its hooks and
 * its current state.
 */
struct i915_global_obj {
	struct i915_global_obj *next;      /* list_head, appended at the TAIL */
	struct i915_global_state *state;
	const struct i915_global_state_funcs *funcs;
	/*
	 * Diagnostic only; the reference's intel_global_obj has no name.  Set by
	 * the caller AFTER intel_atomic_global_obj_init (which memsets the obj).
	 */
	const char *name;
};

/* Per-user global state objects (the reference's *_state->base members). */
struct i915_cdclk_obj_state {
	struct i915_global_state base;
};
/*
 * The DBUF global state: which slices are enabled.
 */
struct i915_dbuf_obj_state {
	struct i915_global_state base;
};
/*
 * The bandwidth global state: the QGV points the display may use.
 */
struct i915_bw_obj_state {
	struct i915_global_state base;
	uint16_t qgv_points_mask;
};
/*
 * The PM demand global state.
 */
struct i915_pmdemand_obj_state {
	struct i915_global_state base;
};

/*
 * The cursor/max-size ladders in intel_mode_config_init() branch on platforms
 * that predate everything this port targets.  They are kept as real branches
 * (so the ladder itself is exercised) and selected by this enum; ADL-P is
 * I915_PLAT_NONE and takes the modern "else" arms.
 */
enum i915_legacy_platform {
	I915_PLAT_NONE = 0,   /* ADL-P and every other modern part */
	I915_PLAT_I845G,
	I915_PLAT_I865G,
	I915_PLAT_I830,
	I915_PLAT_I85X,
	I915_PLAT_I915G,
	I915_PLAT_I915GM,
	I915_PLAT_BROADWELL,
	I915_PLAT_SKYLAKE,
	I915_PLAT_BROXTON
};

/*
 * The mode configuration limits of the display (the Linux drm_mode_config
 * the display fills in).
 */
struct i915_mode_config {
	unsigned min_width, min_height;
	unsigned max_width, max_height;
	unsigned cursor_width, cursor_height;
	unsigned preferred_depth;
	int prefer_shadow;
	int async_page_flip;
	int funcs_set;             /* mode_config->funcs = &intel_mode_funcs */
	int helper_private_set;    /* ->helper_private = &intel_mode_config_funcs */
	int drm_mode_config_inited;
};

/*
 * ---------------------------------------------------------------- *
 * intel_fbc_init()
 * ----------------------------------------------------------------
 */

#define I915_MAX_FBCS 4      /* I915_MAX_FBCS */

enum i915_fbc_id {
	I915_FBC_A = 0,
	I915_FBC_B,
	I915_FBC_C,
	I915_FBC_D
};

/* Which intel_fbc_funcs vtable intel_fbc_create() selected. */
enum i915_fbc_funcs_kind {
	I915_FBC_FUNCS_NONE = 0,
	I915_FBC_FUNCS_I8XX,
	I915_FBC_FUNCS_I965,
	I915_FBC_FUNCS_G4X,
	I915_FBC_FUNCS_ILK,
	I915_FBC_FUNCS_SNB,
	I915_FBC_FUNCS_IVB
};

/*
 * One frame-buffer compression instance and its lock.
 */
struct i915_fbc {
	int id;                            /* enum i915_fbc_id */
	int funcs_kind;                    /* enum i915_fbc_funcs_kind */
	struct i915_work underrun_work; /* INIT_WORK(&fbc->underrun_work, ...) */
	struct mutex lock;                 /* mutex_init(&fbc->lock) */
	int lock_inited;
	int in_use;
};

/* enum intel_quirk_id, values preserved (used as BIT(quirk)). */
enum i915_quirk_id {
	I915_QUIRK_BACKLIGHT_PRESENT = 0,
	I915_QUIRK_INCREASE_DDI_DISABLED_TIME,
	I915_QUIRK_INCREASE_T12_DELAY,
	I915_QUIRK_INVERT_BRIGHTNESS,
	I915_QUIRK_LVDS_SSC_DISABLE,
	I915_QUIRK_NO_PPS_BACKLIGHT_POWER_HOOK,
	I915_QUIRK_FW_SYNC_LEN
};

/*
 * ---------------------------------------------------------------- *
 * Device-owned display state of the noirq tail
 * ----------------------------------------------------------------
 */

struct i915_display_state {
	/* display.global.obj_list (INIT_LIST_HEAD in intel_mode_config_init) */
	struct i915_global_obj *obj_list_head;
	struct i915_global_obj *obj_list_tail;
	unsigned obj_count;
	int obj_list_inited;

	struct i915_mode_config mode_config;

	/* The four global state objects + their device-owned storage. */
	struct i915_global_obj cdclk_obj, dbuf_obj, bw_obj, pmdemand_obj;
	struct i915_cdclk_obj_state cdclk_state;
	struct i915_dbuf_obj_state dbuf_state;
	struct i915_bw_obj_state bw_obj_state;
	struct i915_pmdemand_obj_state pmdemand_state;

	/* intel_color_init */
	int color_done;                 /* ran and returned 0 */

	/* intel_bw_init / icl_force_disable_sagv */
	int sagv_force_disable_attempted;
	int sagv_pcode_ret;             /* icl_pcode_restrict_qgv_points() result */
	unsigned sagv_qgv_points;       /* icl_max_bw_qgv_point_mask() */
	unsigned sagv_psf_points;       /* icl_max_bw_psf_gv_point_mask() */

	/* intel_pmdemand_init */
	int pmdemand_wa_14016740474;    /* ver-14 A0..C0 WA applied (never on ADL-P) */

	/* intel_init_quirks */
	unsigned quirk_mask;            /* display.quirks.mask */
	unsigned quirk_hooks_fired;
	int dmi_scanned;                /* the DMI list was walked */
	int dmi_available;              /* a DMI backend answered */

	/* intel_fbc_init */
	int enable_fbc_param;           /* display.params.enable_fbc IN (-1 = auto) */
	int enable_fbc_sanitized;
	unsigned fbc_mask;              /* DISPLAY_RUNTIME_INFO()->fbc_mask, post-WA */
	int fbc_vtd_wa;                 /* need_fbc_vtd_wa() fired */
	struct i915_fbc *fbc[I915_MAX_FBCS];
	struct i915_fbc fbc_store[I915_MAX_FBCS];
	unsigned fbc_created;

	/* diagnostics */
	unsigned state_allocs;          /* successful global-state allocations */
	const char *fail_where;
	int inited;
};

/*
 * The output setup, readout and sanitize (takeover.c).
 */
/*
 * intel_display_driver_probe_nogem().
 *
 * covers the front section, up to (but not including) intel_setup_outputs():
 *
 *   intel_wm_init            -> skl_wm_init: intel_sagv_init + skl_setup_wm_latency
 *   intel_panel_sanitize_ssc -> LVDS/SSC; nothing to do on ADL-P
 *   intel_pps_setup          -> pps.mmio_base
 *   intel_gmbus_setup        -> gmbus.mmio_base, the ICP pin table, GMBUS reset
 *   intel_crtc_init x4       -> per-pipe CRTC + plane records
 *   intel_plane_possible_crtcs_init / intel_shared_dpll_init /
 *   intel_fdi_pll_freq_update / intel_update_czclk
 *   intel_display_driver_init_hw -> intel_update_cdclk + adlp_display_wa_apply
 *   intel_dpll_update_ref_clks / intel_hdcp_component_init /
 *   intel_update_max_cdclk / intel_hti_init / intel_vga_disable
 *
 * The DRM object model (drm_crtc / drm_plane / drm_encoder / drm_connector) is
 * NOT brought in; CRTCs, planes and shared DPLLs are the minimal records
 * the readout and sanitize phases actually consume.
 *
 * Confirmed from the reference device tables rather than assumed, for ADL-P
 * (xe_lpd, DISPLAY_VER 13, PCH_ADP):
 *   - num_scalers[pipe] = 2, num_sprites[pipe] = 4  -> 6 planes/pipe
 *     (1 primary + 4 sprites + 1 cursor)
 *   - has_hti is NOT set on xe_lpd (it is RKL/ADL-S only) -> intel_hti_init is
 *     a no-op here; HDPORT_STATE is not read.
 *   - HAS_HW_SAGV_WM (ver >= 13 && !DGFX) is true -> wm.num_levels = 6, not 8.
 *   - intel_ddi_crt_present() returns false for DISPLAY_VER >= 9.
 */

struct i915_vbt_state;

#define I915_NOGEM_MAX_PIPES    4
#define I915_NOGEM_MAX_PLANES   6    /* primary + 4 sprites + cursor */
#define I915_NOGEM_MAX_DPLLS    10   /* tgl_plls has 9, adlp_plls 7, + terminator */
#define I915_NOGEM_MAX_WM_LVL   8    /* I915_MAX_WM / skl_latency[] */
#define I915_NOGEM_MAX_GMBUS    15   /* pin indices 1..14 */
#define I915_NOGEM_MAX_ENCODERS 8

/* enum port (values preserved; PORT_TC1 aliases PORT_D). */
#define I915_PORT_NONE   (-1)
#define I915_PORT_A      0
#define I915_PORT_B      1
#define I915_PORT_C      2
#define I915_PORT_D      3
#define I915_PORT_TC1    3
#define I915_PORT_TC2    4
#define I915_PORT_TC3    5
#define I915_PORT_TC4    6

/* enum phy (values preserved). */
#define I915_PHY_A 0
#define I915_PHY_B 1
#define I915_PHY_C 2
#define I915_PHY_D 3
#define I915_PHY_E 4
#define I915_PHY_F 5
#define I915_PHY_I 8

/* Why intel_ddi_init() declined to create an encoder (diagnostic). */
enum i915_ddi_skip {
	I915_DDI_OK = 0,
	I915_DDI_SKIP_PORT_NONE,
	I915_DDI_SKIP_STRAP,
	I915_DDI_SKIP_PORT_INVALID,
	I915_DDI_SKIP_PORT_IN_USE,
	I915_DDI_SKIP_DSI,
	I915_DDI_SKIP_HTI,
	I915_DDI_SKIP_NOT_DVI_HDMI_DP,
	I915_DDI_SKIP_EDP_INIT_FAILED     /* intel_ddi_init_dp_connector() failed: the encoder is dropped */
};

/* Which clock vtable intel_ddi_init() selected. */
enum i915_ddi_clk {
	I915_DDI_CLK_NONE = 0,
	I915_DDI_CLK_ICL_COMBO,
	I915_DDI_CLK_ICL_TC
};

/* intel_encoder: the subset the readout and sanitize phases consume. */
struct i915_encoder {
	int port;               /* enum port */
	int phy;                /* enum phy */
	int is_tc;
	int clk_funcs;          /* enum i915_ddi_clk */
	int power_domain;       /* POWER_DOMAIN_PORT_DDI_LANES_* */
	int init_hdmi, init_dp;
	uint8_t dvo_port;
	uint32_t device_type;
	/* readout */
	int crtc_linked;        /* encoder->base.crtc != NULL */
	unsigned pipe_mask;
	int is_mst;
	int in_use;
	int shared_dpll_id;     /* encoder->get_config: the PLL id feeding it (-1 = none / not read) */
	uint32_t dpclka_cfgcr0; /* the value that id was read from */
};

/* intel_plane: the subset the readout/sanitize phases use. */
enum i915_plane_type {
	I915_PLANE_PRIMARY = 0,
	I915_PLANE_SPRITE,
	I915_PLANE_CURSOR
};

/*
 * One plane record of the output setup: its id, pipe and the state the
 * readout found.
 */
struct i915_plane {
	int id;                 /* enum plane_id: PLANE_PRIMARY=0, SPRITE0.., CURSOR */
	int type;               /* enum i915_plane_type */
	unsigned pipe;
	int visible;            /* plane_state->uapi.visible (readout) */
	int in_use;
};

/* intel_crtc + the parts of intel_crtc_state the nogem probe fills in. */
struct i915_crtc_state {
	int active;             /* hw.active */
	int enable;             /* hw.enable */
	int cpu_transcoder;     /* -1 = INVALID_TRANSCODER */
	int inherited;
	unsigned active_planes;
	/* readout results */
	uint32_t transconf;
	uint32_t trans_ddi_func_ctl;
	unsigned enabled_transcoders;   /* hsw_enabled_transcoders() bitmask */
	/* intel_get_transcoder_timings() */
	unsigned hdisplay, htotal, vdisplay, vtotal;
	unsigned pipe_src_w, pipe_src_h;
	int power_gated;                /* the pipe/transcoder power was off */
};

/*
 * One crtc record of the output setup: its pipe and the state the readout
 * found.
 */
struct i915_crtc {
	unsigned pipe;
	unsigned num_scalers;
	unsigned plane_ids_mask;
	int active;             /* crtc->active */
	int enabled;            /* crtc->base.enabled */
	struct i915_plane planes[I915_NOGEM_MAX_PLANES];
	unsigned num_planes;
	struct i915_crtc_state state;
	int fifo_underrun_reporting;
	int in_use;
};

/* intel_shared_dpll (adlp_plls). */
enum i915_dpll_funcs {
	I915_DPLL_FUNCS_NONE = 0,
	I915_DPLL_FUNCS_COMBO,
	I915_DPLL_FUNCS_TBT,
	I915_DPLL_FUNCS_DKL
};

/*
 * One shared DPLL record: its id, its hooks and the state the readout
 * found.
 */
struct i915_xd;
struct i915_xd_pll_desc;

struct i915_dpll {
	const char *name;
	int id;                 /* enum intel_dpll_id */
	int funcs;              /* enum i915_dpll_funcs */
	unsigned index;
	uint32_t enable_reg;
	const struct i915_xd_pll_desc *xd_pll;  /* the platform's PLL (xd clock) */
	int on;                 /* readout */
	unsigned pipe_mask;
	unsigned active_mask;
	int readout_incomplete; /* an active pipe's PLL could not be read (e.g. TC get_pll not ported): never disable */
};

/* One ICP gmbus pin (the adapter itself needs an i2c core we do not have). */
struct i915_gmbus_pin {
	const char *name;
	unsigned gpio;
	unsigned reg0;          /* pin | GMBUS_RATE_100KHZ */
	int present;
};

/*
 * The records the output setup, the readout and the sanitize build and
 * read: crtcs, planes, DPLLs, encoders and their locks.
 */
struct i915_display_nogem {
	/* intel_wm_init -> skl_wm_init */
	unsigned wm_num_levels;
	uint16_t wm_skl_latency[I915_NOGEM_MAX_WM_LVL];
	int wm_latency_valid;
	int sagv_status;                /* mirrors display.sagv.status */
	uint32_t sagv_block_time_us;

	/* intel_pps_setup / intel_gmbus_setup */
	uint32_t pps_mmio_base;
	uint32_t gmbus_mmio_base;
	struct mutex gmbus_lock;
	struct wait_queue gmbus_waitq;
	struct i915_gmbus_pin gmbus_pins[I915_NOGEM_MAX_GMBUS];
	unsigned gmbus_pins_present;
	int gmbus_adapters_unimplemented;   /* i2c core absent: recorded, not faked */

	/* The xd display the DPLL readout and sanitize go through; NULL without a description. */
	struct i915_xd *xd;

	/* CRTCs / planes / DPLLs */
	struct i915_crtc crtcs[I915_NOGEM_MAX_PIPES];
	unsigned num_crtcs;
	struct i915_dpll dplls[I915_NOGEM_MAX_DPLLS];
	unsigned num_dplls;
	int dpll_mgr_present;
	struct mutex dpll_lock;
	uint32_t dpll_ref_nssc;

	/* cdclk-derived */
	uint32_t max_cdclk_freq;
	int cdclk_logical_set;          /* cdclk_state->logical = actual = hw */

	/* WAs / misc */
	int adlp_wa_applied;
	/* the xe_d arm of intel_display_wa_apply() ran (Tiger Lake and friends) */
	int xe_d_wa_applied;
	int hti_state_read;             /* has_hti is 0 on xe_lpd -> stays 0 */
	uint32_t hti_state;
	int fdi_pll_freq_updated;       /* ADL-P: the function returns early */
	int czclk_updated;              /* ADL-P: the function returns early */
	int hdcp_component_unimplemented;

	/* intel_vga_disable */
	int vga_already_disabled;
	int vga_disable_done;

	/* intel_setup_outputs */
	struct i915_encoder encoders[I915_NOGEM_MAX_ENCODERS];
	unsigned num_encoders;
	unsigned ddi_init_calls;
	unsigned ddi_skipped;
	int ddi_skip_reason[I915_NOGEM_MAX_ENCODERS];
	int ddi_skip_port[I915_NOGEM_MAX_ENCODERS];
	unsigned num_ddi_skips;
	int crt_present;
	int outputs_done;
	/*
	 * intel_ddi_init() -> intel_ddi_init_dp_connector() -> intel_dp_init_connector() ->
	 * intel_edp_init_connector(), for a DP-capable encoder: 1 = not an eDP port (nothing
	 * done), 0 = eDP initialised and KEPT by the hook's owner, < 0 = failed (the reference
	 * then frees the encoder).  NULL = no connector construction (the pre-behaviour).
	 */
	int (*dp_connector_init)(void *ctx, int port);
	void *dp_connector_ctx;
	int edp_port;                       /* port whose eDP connector is live, or -1 */
	int edp_init_rc;

	/* intel_modeset_readout_hw_state */
	unsigned active_pipes;          /* cdclk_state/dbuf_state->active_pipes */
	unsigned readout_crtcs;
	unsigned readout_planes_visible;
	unsigned readout_encoders_linked;
	unsigned readout_dplls_on;
	int readout_done;
	/*
	 * Fields the reference reads that this port deliberately does NOT: color
	 * config, DSC, VRR, bigjoiner, scaler detail, output_format, linetime,
	 * pixel_multiplier, framestart_delay.  None of them feeds a sanitize
	 * decision, and inventing them would be worse than recording the gap.
	 */
	int readout_detail_unimplemented;

	/* intel_modeset_setup_hw_state, sanitize half */
	int early_display_was_applied;   /* IS_DISPLAY_VER(10,12) only -> 0 on ADL-P */
	int pch_sanitize_applied;        /* HAS_PCH_IBX only -> 0 */
	int plane_mapping_sanitized;     /* DISPLAY_VER >= 4 returns -> 0 */
	unsigned vblank_resets;
	unsigned dmc_pipes_enabled;
	unsigned vblank_on_count;
	unsigned fbc_deactivated;
	unsigned crtcs_disabled_noatomic;
	unsigned encoder_clocks_gated;
	unsigned dplls_disabled;
	int cmtg_wa_applied;             /* ADL-P A0..B0 + DPLL0 only -> 0 on D0 */
	unsigned wells_disabled;
	int wm_hw_state_read;
	/*
	 * intel_crtc_disable_noatomic() would need the full modeset disable path
	 * (hsw_crtc_disable: encoder post_disable, DDI, DPLL, pipe, DBUF).  It is
	 * only reachable from an ACTIVE pipe with no encoders; when that happens
	 * this flag is raised instead of pretending the pipe was disabled.
	 */
	int crtc_disable_noatomic_unimplemented;
	int sanitize_done;

	/* diagnostics */
	unsigned mmio_writes;
	const char *fail_where;
	int inited;
};

/*
 * The Linux driver probe (display.c, hotplug.c, power.c).
 */
/*
 * the rest of i915_driver_probe() after i915_gem_init().
 *
 * The reference (i915_driver.c, 6.8.12):
 *   intel_pxp_init(i915)                 see pxp.h
 *   intel_display_driver_probe(i915)     display/intel_display_driver.c:
 *     intel_initial_commit()             an atomic commit that recomputes the
 *                                        plane state of every ACTIVE crtc; with
 *                                        no active crtc it commits nothing
 *     intel_overlay_setup()              HAS_OVERLAY: gen2-4 only
 *     intel_fbdev_init()                 CONFIG_DRM_FBDEV_EMULATION
 *     intel_hpd_init()                   every pin HPD_ENABLED, then
 *                                        hpd_irq_setup = gen11_hpd_irq_setup:
 *                                        GEN11_DE_HPD_IMR, TC/TBT_HOTPLUG_CTL,
 *                                        and (PCH >= ICP) SHPD_FILTER_CNT,
 *                                        SDEIMR, SHOTPLUG_CTL_DDI/TC
 *     intel_hpd_poll_disable()           poll_enabled = false + poll_init_work
 *     skl_watermark_ipc_init()           HAS_IPC: DISP_ARB_CTL2.DISP_IPC_ENABLE
 *   i915_driver_register(i915):
 *     gem/pmu/vgpu/drm_dev/debugfs/sysfs/perf/gt sysfs/hwmon registrations
 *     intel_display_driver_register()    opregion register, acpi video, audio
 *                                        components, debugfs, fbdev config,
 *                                        drm_kms_helper_poll_init
 *     intel_power_domains_enable()       PUT the POWER_DOMAIN_INIT reference
 *                                        held since intel_power_domains_init_hw:
 *                                        every well with no other reference
 *                                        turns off, DC_off last (DC6 allowed)
 *     intel_runtime_pm_enable()          autosuspend 10 s, drop the probe ref
 *     dsm handler, vga switcheroo
 *   and the remove path mirrors it: intel_runtime_pm_disable (take the ref
 *   back), intel_power_domains_disable (get INIT again), display unregister.
 *
 * ADAPTATIONS (recorded):
 *   - No DRM object model here: intel_initial_commit is executed only in its
 *     no-active-crtc form (an active crtc is reported as unimplemented);
 *     connectors do not exist, so the poll_init work touches no connector
 *     (the DISPLAY_CORE power get/put around it is kept); the userspace-facing
 *     registrations (drm_dev, debugfs, sysfs, pmu, perf, hwmon, audio
 *     components, fbdev, acpi video, dsm, switcheroo) are recorded as N/A.
 *   - The poll_init work runs inline where the reference queues it.
 *   - intel_runtime_pm_enable drops the probe reference but does not arm
 *     autosuspend: intel_runtime_suspend() (what the PM core would run 10 s
 *     later) is not ported, so the device stays in D0.
 *   - intel_power_domains_verify_state() is compiled only under
 *     CONFIG_DRM_I915_DEBUG_RUNTIME_PM in the reference; here its per-well
 *     refcount/HW comparison is kept as a diagnostic.
 */

struct i915_rpm;
struct i915_display_core;
struct i915_display_nogem;

/* enum hpd_pin (intel_display_limits.h) */
#define I915_HPD_NONE      0
#define I915_HPD_PORT_A    4
#define I915_HPD_PORT_B    5
#define I915_HPD_PORT_C    6
#define I915_HPD_PORT_D    7
#define I915_HPD_PORT_E    8
#define I915_HPD_PORT_TC1  9
#define I915_HPD_PORT_TC6  14
#define I915_HPD_NUM_PINS  15

/* hotplug.stats[].state */
#define I915_HPD_ENABLED        0
#define I915_HPD_DISABLED       1
#define I915_HPD_MARK_DISABLED  2

/* Registers (i915_reg.h). */
#define I915_GEN11_DE_HPD_IMR       0x44474u
#define I915_GEN11_TBT_HOTPLUG_CTL  0x44030u
#define I915_GEN11_TC_HOTPLUG_CTL   0x44038u
/* The DP-alt (bits 16 to 21) and Thunderbolt (bits 0 to 5) hotplug bits of TC1 to TC6 in GEN11_DE_HPD_IIR. */
#define I915_GEN11_DE_TC_HOTPLUG_MASK   0x003f0000u
#define I915_GEN11_DE_TBT_HOTPLUG_MASK  0x0000003fu
#define I915_SDEIMR                 0xc4004u
#define I915_SHOTPLUG_CTL_DDI       0xc4030u
#define I915_SHOTPLUG_CTL_TC        0xc4034u
#define I915_SHPD_FILTER_CNT        0xc4038u
#define I915_SHPD_FILTER_CNT_500_ADJ 0x001d9u
#define I915_SHPD_FILTER_CNT_250     0x000f8u
#define I915_DISP_ARB_CTL2          0x45004u
#define I915_DISP_IPC_ENABLE        (1u << 3)

/*
 * The hotplug state of the Linux driver probe: the poll work and the pins
 * it watches.
 */
struct i915_hotplug {
	/* intel_hpd_init_pins(): hpd_gen11 / hpd_icp */
	uint32_t hpd[I915_HPD_NUM_PINS];
	uint32_t pch_hpd[I915_HPD_NUM_PINS];
	int pins_inited;

	/* encoder->hpd_pin of every encoder intel_setup_outputs() created */
	int encoder_pin[8];
	unsigned n_encoders;

	int state[I915_HPD_NUM_PINS];
	unsigned count[I915_HPD_NUM_PINS];
	int poll_enabled;          /* display.hotplug.poll_enabled */
	int kms_poll_inited;       /* drm_kms_helper_poll_init(): mode_config.poll_enabled */

	/* what the last hpd_irq_setup computed and left in the registers */
	unsigned irq_setups;
	int irq_setup_skipped;     /* !display_irqs_enabled */
	uint32_t de_enabled_irqs, de_hotplug_irqs, pch_enabled_irqs, pch_hotplug_irqs;
	uint32_t de_hpd_imr, tc_ctl, tbt_ctl, shpd_filter, sdeimr, shotplug_ddi, shotplug_tc;
	int sdeimr_skipped;        /* ibx_display_interrupt_update: !intel_irqs_enabled */
	unsigned poll_init_works;
	unsigned poll_core_gets;
};

/*
 * What the Linux display driver probe leaves after its noirq stage:
 * hotplug, registrations and the IPC setting.
 */
struct i915_driver_probe {
	struct i915_hotplug hp;

	/* intel_display_driver_probe */
	unsigned active_crtcs;
	int initial_commit_rc;
	int initial_commit_unimplemented;   /* an active crtc needs the atomic commit */
	int power_domains_enable_deferred;  /* the INIT reference is KEPT until the takeover */
	int overlay;                        /* HAS_OVERLAY */
	int fbdev;                          /* CONFIG_DRM_FBDEV_EMULATION */
	int ipc_enabled;

	/* i915_driver_register */
	int opregion_registered;
	unsigned na_registrations;          /* userspace-facing steps recorded N/A */
	unsigned wells_on_before, wells_on_after;
	int dc_state_after;                 /* DC_STATE_EN & mask after the INIT put */
	unsigned verify_mismatches;
	int rpm_usage_after;
	int registered;

	int err;
	const char *err_where;
};
/*
 * The DRM device and vblank workers (device.c).
 */
/*
 * minimal DRM device management + vblank (see device.c).
 *
 * Ports the drm_dev_init() side of DRM device state, a drmm managed-resource
 * list with the drm_add_action_or_reset() contract, and drm_vblank_init() /
 * drm_vblank_worker_init() for the display "noirq" bring-up.  It does not
 * open /dev/dri, start display output, or wait on real vblanks.
 */

struct i915_device;

#define I915_DRM_MAX_PIPES  4    /* ADL-P pipe_mask = A|B|C|D */
#define I915_DRMM_MAX      16    /* managed cleanup-action slots */

/* A managed cleanup action (drmm_add_action_or_reset). */
struct i915_drmm_action {
	void (*fn)(void *arg);
	void *arg;
};

/* Per-CRTC vblank state (drm_vblank_crtc subset). */
struct i915_drm_vblank_crtc {
	struct spinlock lock;        /* per-CRTC vblank lock */
	struct wait_queue queue;     /* vblank wait queue */
	unsigned pipe;               /* CRTC/pipe index */
	uint64_t count;              /* vblank sequence (seqlock-guarded in the reference) */
	uint32_t seqlock;            /* seqlock generation (even = stable) */
	int disable_timer_inited;    /* vblank-disable timer state (modelled) */
	uint64_t disable_deadline;
	struct i915_workqueue worker;  /* per-CRTC vblank worker (drm_vblank_worker_init) */
	int worker_created;
	int inited;
};

/* DRM device management state (drm_device subset). */
struct i915_drm_device {
	struct i915_device *parent;  /* back-reference to the parent device */
	uint32_t driver_features;    /* DRIVER_* feature flags subset */
	int open_count;              /* device open refcount */

	struct spinlock managed_lock;/* drmm managed-resource list lock */
	struct i915_drmm_action drmm[I915_DRMM_MAX];
	unsigned drmm_count;

	struct spinlock event_lock;  /* event list lock */
	int minor_registered;        /* minor bookkeeping; 0 = not published */

	unsigned num_crtcs;          /* INTEL_NUM_PIPES fully initialised */
	int vblank_disable_immediate;
	struct i915_drm_vblank_crtc vblank[I915_DRM_MAX_PIPES];
	int vblank_inited;

	int inited;
};
/*
 * The VGA plane and arbiter (takeover.c).
 */
/* video/internal.h resource flags returned by the decode callback. */
#define I915_VGA_RSRC_LEGACY_IO   0x01u
#define I915_VGA_RSRC_LEGACY_MEM  0x02u
#define I915_VGA_RSRC_NORMAL_IO   0x04u
#define I915_VGA_RSRC_NORMAL_MEM  0x08u

/* VGA arbiter client state (a faithful subset of the registration). */
struct i915_vga_client {
	struct drv_pci_device *gpu;   /* display device (arbiter client) */
	struct drv_pci_device *gmch;  /* host bridge 00:00.0 (GMCH_CTRL owner) */
	unsigned display_ver;         /* selects SNB_GMCH_CTRL vs INTEL_GMCH_CTRL */
	int registered;               /* callback installed (0 after unregister) */
};

/*
 * Legacy VGA I/O accessor (intel_vga_reset_io_mem's get/in/out/put).  In
 * production these are the real arbiter + port I/O; a GPU-free test injects a
 * recorder so the arbiter get -> MIS_R read -> MIS_W write -> put sequence is
 * checked without touching real VGA ports.  get() returns 1 on success, 0 if
 * the legacy-IO resource could not be acquired (then no I/O and no put).
 */
struct i915_vga_io_ops {
	int (*get)(void *ctx, int rsrc);
	unsigned char (*in8)(void *ctx, unsigned short port);
	void (*out8)(void *ctx, unsigned short port, unsigned char val);
	void (*put)(void *ctx, int rsrc);
	void *ctx;
};

/*
 * The combo PHY (phy.c).
 */
/* ADL-P combo PHYs. */
#define I915_COMBO_PHY_A  0u
#define I915_COMBO_PHY_B  1u
#define I915_COMBO_PHY_NUM 2u
/*
 * The VBT state (vbt.c).
 */
/* Where the VBT bytes came from.  An explicit blob is NOT an OpRegion. */
enum i915_vbt_origin {
	I915_VBT_ORIGIN_NONE = 0,       /* no VBT: init_vbt_missing_defaults() */
	I915_VBT_ORIGIN_OPREGION,       /* ASLS -> mailbox 4 / RVDA (native path) */
	I915_VBT_ORIGIN_PCI_ROM,        /* $VBT in the PCI expansion ROM */
	I915_VBT_ORIGIN_EXPLICIT_BLOB,  /* a named, hash-pinned blob supplied by the test configuration */
};

#define I915_VBT_MAX_ENCODERS 12
/*
 * One VBT child device as the output setup reads it.
 */
struct i915_vbt_encoder {
	int port;                 /* enum port value (0 = A, 3 = TC1 ...), -1 if none */
	int aux_ch;               /* enum aux_ch value, -1 if none */
	uint16_t device_type;
	uint8_t dvo_port;
	uint8_t ddc_pin_raw;      /* child.ddc_pin as in the VBT */
	int ddc_pin;              /* mapped GMBUS pin (0 = none/invalid) */
	int supports_dp, supports_edp, supports_hdmi, supports_dvi;
	int supports_typec_usb, supports_tbt;
	int lane_reversal, hpd_invert;
	int dp_max_lane_count;    /* 0 = not limited by the VBT */
	int dp_max_link_rate;     /* kHz-per-lane /10 as in the reference (e.g. 270000), 0 = platform max */
	int dp_boost_level, hdmi_boost_level;
	int hdmi_level_shift;     /* -1 = not set */
};

/*
 * The panel data of the VBT: timings, power sequence and backlight.
 */
struct i915_vbt_panel {
	int initialized;
	int panel_type;
	int bpp;                          /* eDP colour depth from the VBT: 18 / 24 / 30 */
	int edp_rate;                     /* DP_LINK_BW_* code the VBT names for fast link */
	int edp_lanes;
	int edp_preemphasis, edp_vswing;
	int edp_low_vswing;
	int edp_hobl;                     /* panel->vbt.edp.hobl */
	int override_afc_startup;         /* display.vbt.override_afc_startup (general features, BDB 249+) */
	int edp_max_link_rate;            /* 0 = not limited */
	/* eDP panel power sequencing, in 100 us units as stored in the VBT */
	uint16_t t1_t3, t8, t9, t10, t11_t12;
	/* backlight */
	int bl_present;
	int bl_type;                      /* enum intel_backlight_type */
	int bl_controller;
	int bl_active_low_pwm;
	uint16_t bl_pwm_freq_hz;
	uint8_t bl_min_brightness;
	/* the VBT's own panel mode, if it carries one (the EDID mode normally wins) */
	int has_lfp_mode;
	int mode_clock_khz;
	uint16_t hdisplay, hsync_start, hsync_end, htotal;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal;
	int drrs_type, vrr;
};

/*
 * What the VBT parser produced for the rest of the display.
 */
struct i915_vbt {
	int inited;
	int origin;                       /* enum i915_vbt_origin */
	const uint8_t *bytes;             /* the validated VBT (not owned) */
	size_t size;
	uint16_t bdb_version;
	char signature[21];
	int missing_defaults_used;
	unsigned num_bdb_blocks;
	unsigned n_encoders;
	struct i915_vbt_encoder enc[I915_VBT_MAX_ENCODERS];
	/* general features the display code consumes */
	int int_lvds_support, display_clock_mode, lvds_use_ssc, lvds_ssc_freq, crt_ddc_pin;
	unsigned arena_used, arena_peak, alloc_failures;
	unsigned log_errors;              /* drm_err / WARN lines the reference emitted */
	/* the xd parse the values come from (vbt-shell.c), or NULL */
	const struct i915_xd_vbt *xd;
};

/*
 * The VBT provider (vbt.c).
 */
/*
 * intel_bios_init (VBT acquisition + parse), see vbt.c.
 *
 * Structure-faithful port of intel_bios_init(): initialise the VBT device/block
 * lists, apply init_vbt_defaults(), then resolve the VBT from (1) the OpRegion
 * (read at the start), else (2) the PCI expansion ROM read for real via config
 * 0x30 + a device mapping.  A found+validated VBT is parsed (BDB blocks, general
 * features/definitions); a genuine absence takes init_vbt_missing_defaults()
 * (default child devices for the non-TC DDI ports).  DGFX SPI flash is NOT read
 * on ADL-P (not IS_DGFX).  This never fabricates a VBT and never fails the probe
 * (intel_bios_init is void): it reports which real source was used.
 */

struct i915_pci;

/*
 * Where the VBT the parser was given came from.
 */
enum i915_vbt_source {
	I915_VBT_SRC_NONE = 0,     /* no VBT found -> missing defaults */
	I915_VBT_SRC_OPREGION,     /* OpRegion mailbox #4 (read at the start) */
	I915_VBT_SRC_PCI_ROM,      /* PCI expansion ROM ($VBT) */
	I915_VBT_SRC_EXPLICIT_BLOB, /* a named, hash-pinned blob the build asked for -- NOT an OpRegion */
};

/* One VBT child device (child_device_config subset used for default gen). */
struct i915_vbt_child {
	unsigned port;               /* source DDI port (diagnostic) */
	uint8_t  dvo_port;           /* DVO_PORT_* */
	uint32_t device_type;        /* DEVICE_TYPE_* bitmask */
};

/* VBT-derived display state (drm_i915_private.display.vbt subset). */
struct i915_vbt_state {
	int has_display;                 /* HAS_DISPLAY snapshot */
	uint16_t version;                /* display.vbt.version */
	int vbt_found;                   /* a real VBT was located + validated */
	int source;                      /* enum i915_vbt_source */
	int missing_defaults_used;       /* init_vbt_missing_defaults() ran */
	unsigned num_bdb_blocks;         /* bdb_blocks list length */
	unsigned num_display_devices;    /* display_devices list length */
	struct i915_vbt_child display_devices[8];

	/* the reference parser's result (vbt.c); device-owned, released by driver_remove */
	struct i915_vbt parsed;
	int parsed_live;

	/* explicit blob bookkeeping (no build fills it since the test VBT was removed; always zero) */
	const char *blob_name;
	unsigned blob_size;
	uint8_t blob_sha256[32];
	int blob_requested, blob_found, blob_hash_ok, blob_subsys_ok, blob_valid;
	uint16_t subsys_vendor, subsys_device;
};

/*
 * Whether the explicit VBT is asked for.
 *
 * Never: the test build has no captured VBT of the test machine, so the kernel takes the VBT
 * from the OpRegion or the PCI ROM, or the Linux defaults.
 *
 * XXX: the explicit-blob bookkeeping it leaves (the EXPLICIT_BLOB source and
 * origin, the blob fields) is dead and can go with the next change of vbt.c.
 */
#define I915_VBT_EXPLICIT 0

/*
 * The OpRegion VBT source (vbt.c).
 */
/*
 * Linux-derived -- where the ACPI OpRegion keeps the VBT (intel_opregion_setup(), v6.8.12).  zedBSD project code;
 * the offsets and the decision order are the reference's (struct opregion_header / struct opregion_asle,
 * OPREGION_*_OFFSET, MBOX_*).  Pure: it reads a copy of the 8 KiB OpRegion and says where the VBT is; the caller maps
 * and validates it (intel_bios_is_valid_vbt).  intel_load_vbt_firmware() and the DMI quirk list are not consulted here.
 */

#define I915_OPREGION_SIZE        0x2000u
#define I915_OPREGION_ASLE_OFFSET 0x300u
#define I915_OPREGION_VBT_OFFSET  0x400u
#define I915_OPREGION_ASLE_EXT    0x1c00u

/*
 * Where in the OpRegion the VBT was found.
 */
enum i915_opregion_vbt_src { I915_OPVBT_NONE = 0, I915_OPVBT_RVDA, I915_OPVBT_MAILBOX4 };

/*
 * What the OpRegion check found: its header, mailboxes and VBT location.
 */
struct i915_opregion_info {
	int signature_ok;
	uint32_t size_kib, mboxes;
	uint8_t major, minor, revision;
	uint64_t rvda;                  /* as stored */
	uint32_t rvds;
	int rvda_relative;              /* 2.1+: an offset from the OpRegion base */
	int rvda_inside;                /* 2.1+ and rvda < OPREGION_SIZE: the reference WARNs */
	/* the candidate the reference tries first: RVDA (physical address), else mailbox #4 (offset in the OpRegion) */
	int src;
	uint64_t vbt_phys;              /* RVDA */
	uint32_t vbt_offset, vbt_max;   /* mailbox #4: offset and the size the reference allows */
	/* runtime mailboxes as the firmware (or a previous driver) left them -- OBSERVED, never written by this driver */
	uint32_t acpi_drdy, acpi_csts, acpi_cevt, acpi_chpd, acpi_clid;   /* struct opregion_acpi at 0x100 */
	uint32_t asle_ardy, asle_aslc, asle_tche;                         /* struct opregion_asle at 0x300 */
};
/*
 * The firmware display check (takeover.c).
 */
/*
 * The output the firmware (GOP) was scanning out when the driver started
 * (the GPU scanout rule): the kind of interface, the pipe and
 * the port (enum port, PORT_NONE when none), the transcoder's mode select
 * (TRANS_DDI_FUNC_CTL bits 26:24), and every pipe the firmware had lit.
 */
#define I915_GOP_NONE		0U	/* no pipe was lit */
#define I915_GOP_EDP		1U	/* DDI A in DP SST mode: the built-in panel */
#define I915_GOP_HDMI		2U	/* DDI B in HDMI or DVI mode: the driver's HDMI path */
#define I915_GOP_OTHER		3U	/* an interface this driver cannot light yet */
#define I915_GOP_DP_TC		4U	/* a Type-C port's DDI in DP SST mode (DP-alt): the external DP path */

struct i915_gop_output {
	unsigned kind;
	unsigned pipe;
	int port;
	unsigned mode;
	unsigned pipes;
};

/*
 * The OpRegion as DATA (VBT_ONLY): the start maps it read-only, copies the 8 KiB and the VBT it names, checks the
 * VBT.  The runtime protocol (ACPI notifier, drdy / ardy / chpd / csts / DIDL / CADL, ASLE) is NOT joined: runtime =
 * DISABLED, reason ACPI_RUNTIME_UNAVAILABLE.  The mailboxes' current values are observed only.
 */
struct i915_opregion_data {
	int present;                    /* ASLS != 0 */
	int mapped;
	struct i915_opregion_info op;
	int vbt_valid;
	const uint8_t *vbt;             /* the copy handed to the parser (0 = none) */
	uint32_t vbt_size;
	uint8_t vbt_sha256[32];
	int runtime_enabled;            /* always 0 here */
	const char *runtime_reason;
};

/*
 * The eDP sink bring-up (dp-sink.c).
 */
/*
 * eDP panel bring-up, first stage: panel power sequencer
 * (PPS / VDD) ownership and the AUX channel, up to the sink's DPCD and EDID.
 *
 * The work is done by the reference's own functions (generated files in this
 * directory: panel.c, aux.c, dp-sink.c,
 * edid-read.c) in the order of intel_edp_init_connector().  This header is
 * the zedBSD-facing surface: plain C types only, so the probe and the GPU-free
 * tests can use it without the reference's environment.
 *
 * Every hardware and time dependency goes through struct i915_dp_env, so the
 * same production code runs on the real GPU (dp-sink.c binds it to the
 * MMIO / wait / power-domain layers) and on the register model (dp_fake_hw.c).
 */

struct i915_dp_env {
	void *ctx;
	uint32_t (*read32)(void *ctx, uint32_t reg);
	void (*write32)(void *ctx, uint32_t reg, uint32_t value);
	/* __intel_wait_for_register(): 0, or a negative errno (-ETIMEDOUT); *out (may be NULL) = last value */
	int (*wait_reg)(void *ctx, uint32_t reg, uint32_t mask, uint32_t value,
		unsigned fast_us, unsigned slow_ms, uint32_t *out);
	void (*sleep_us)(void *ctx, unsigned us);
	uint64_t (*now_ms)(void *ctx);                  /* monotonic */
	/*
	 * the register window the AUX transactions (xd-aux) and the panel power
	 * sequencer (xd-panel) run through, both under the PPS mutex: the same
	 * registers as read32/write32, with waits of its own; the binding that
	 * sets the hooks above sets it too
	 */
	struct i915_xd_io *aux_io;
	int (*power_get)(void *ctx, int domain);        /* enum i915_power_domain value; 0 = ok */
	void (*power_put)(void *ctx, int domain);
	/*
	 * intel_display_power_put_async(): the reference leaves the DP layer now; the power layer
	 * parks it, hands it back to the next get of that domain, or releases it ~100 ms later
	 */
	void (*power_put_async)(void *ctx, int domain);
	/* the PPS mutex and the AUX hardware mutex (I915_DP_LOCK_*): real mutual exclusion */
	void (*lock)(void *ctx, int which);
	void (*unlock)(void *ctx, int which);
	/*
	 * delayed work (I915_DP_WORK_*): queue = 1 when newly queued (0: already pending);
	 * cancel = 1 when it was pending; with sync != 0 it also waits until the work body is
	 * not running -- the caller then must not hold a lock the body takes.  The backend runs
	 * the body by calling i915_edp_work_run(which) from a context that may sleep.
	 */
	int (*delayed_queue)(void *ctx, int which, unsigned delay_ms);
	int (*delayed_cancel)(void *ctx, int which, int sync);
	int (*delayed_pending)(void *ctx, int which);
	/* bookkeeping kept by the DP layer (read by the tests and the run log) */
	int power_refs[2];                              /* [0] DISPLAY_CORE, [1] the AUX domain */
	unsigned power_get_failures;
	unsigned power_put_underflows;
	unsigned sleeps;
	uint64_t slept_us;
	unsigned lock_errors;                           /* recursion / unlock of a free lock */
	unsigned async_puts;
};

#define I915_DP_LOCK_PPS 0
#define I915_DP_LOCK_AUX 1
#define I915_DP_WORK_VDD_OFF 0        /* edp_panel_vdd_work */

#define I915_EDP_MAX_EDID_BLOCKS 4u

/*
 * Result codes are the reference's negative errnos in LINUX numbering (zedBSD's
 * <errno.h> numbers differ): rc == -I915_EDP_ETIMEDOUT and so on.
 */
#define I915_EDP_EIO 5
#define I915_EDP_ENXIO 6
#define I915_EDP_E2BIG 7
#define I915_EDP_EBUSY 16
#define I915_EDP_EINVAL 22
#define I915_EDP_EPROTO 71
#define I915_EDP_ETIMEDOUT 110
#define I915_EDP_EREMOTEIO 121

/*
 * How to reach the eDP panel: port, AUX channel, raw clock and the VBT
 * power sequence.
 */
struct i915_edp_config {
	int port;                 /* enum port: 0 = A */
	int aux_ch;               /* enum aux_ch from the VBT child: 0 = A */
	uint32_t rawclk_khz;
	/*
	 * the VBT panel's eDP power sequence (100 us units) and PPS / backlight controller index;
	 * all zero when the panel type is not known before the EDID (the reference's "early" state)
	 */
	uint16_t t1_t3, t8, t9, t10, t11_t12;
	int bl_controller;
	int log_level;            /* 0 errors, 1 +info, 2 +debug */
};

/*
 * The four panel power sequencer registers of one sequencer.
 */
struct i915_edp_pps_regs { uint32_t pp_status, pp_control, pp_on_delays, pp_off_delays; };

/*
 * How far the eDP sink bring-up got.
 */
enum i915_edp_stage {
	I915_EDP_STAGE_NONE = 0,
	I915_EDP_STAGE_PPS_INIT,        /* intel_pps_init() */
	I915_EDP_STAGE_DPCD,            /* intel_edp_init_dpcd(): receiver caps + eDP display control */
	I915_EDP_STAGE_EDID,            /* drm_edid_read_ddc(): I2C-over-AUX */
	I915_EDP_STAGE_ACQUIRED,        /* waiting for i915_edp_init_late() */
	I915_EDP_STAGE_LATE,            /* intel_pps_init_late() done: VDD-off is scheduled */
	I915_EDP_STAGE_ENDED,
};

/*
 * What the eDP sink bring-up found and did: the PPS registers, DPCD, EDID
 * and who holds what.
 */
struct i915_edp_result {
	int stage;                        /* the last stage COMPLETED */
	int rc;                           /* 0, or the negative errno of the stage that failed */
	int failed_stage;

	int pps_idx, pps_valid;
	struct i915_edp_pps_regs before;       /* as found */
	struct i915_edp_pps_regs after_init;   /* after intel_pps_init() programmed the delays */
	struct i915_edp_pps_regs after_acquire;/* VDD is expected ON here */
	struct i915_edp_pps_regs after_end;    /* after intel_pps_vdd_off_sync() */
	int delay_power_up_ms, delay_power_down_ms, delay_power_cycle_ms, delay_bl_on_ms, delay_bl_off_ms;

	uint8_t dpcd[15];                 /* DP_RECEIVER_CAP_SIZE */
	int dpcd_ok;
	uint8_t edp_dpcd[3];              /* EDP_DISPLAY_CTL_CAP_SIZE, from 0x700 */
	int edp_dpcd_ok;
	uint8_t link_cfg[2];              /* DP_LINK_BW_SET / DP_LANE_COUNT_SET as found (not written) */
	int link_cfg_ok;
	uint8_t edid[128u * I915_EDP_MAX_EDID_BLOCKS];
	unsigned edid_blocks;             /* valid blocks read */
	unsigned edid_extensions;         /* the base block's extension count */
	int edid_ok;

	/* ownership at the end of each phase */
	int vdd_wanted, vdd_on_hw, vdd_wakeref_held, vdd_work_pending;
	int power_refs_core, power_refs_aux;
	unsigned power_get_failures, power_put_underflows;
	unsigned i2c_defers, i2c_nacks;
	unsigned log_errors;              /* drm_err / WARN lines the reference emitted */
	uint64_t elapsed_ms;
};

/*
 * The modeset hooks (modeset.c).
 */
/* panel power sequencer / backlight-power operations, executed by the resident eDP (dp-sink.c) */
enum i915_lcd_panel_op {
	I915_LCD_PANEL_ON = 0,          /* intel_pps_on() */
	I915_LCD_PANEL_OFF,             /* intel_pps_off() */
	I915_LCD_PANEL_VDD_ON,          /* intel_pps_vdd_on() */
	I915_LCD_PANEL_VDD_OFF_SYNC,    /* intel_pps_vdd_off_sync() */
	I915_LCD_PANEL_BACKLIGHT_ON,    /* intel_pps_backlight_on(): the PPS's backlight-enable bit */
	I915_LCD_PANEL_BACKLIGHT_OFF,   /* intel_pps_backlight_off() */
};

#define I915_LCD_LOCK_DPLL 0           /* i915->display.dpll.lock */
#define I915_LCD_LOCK_BACKLIGHT 1      /* i915->display.backlight.lock */

/* points of the commit at which the caller may look at the hardware (no writes of the path depend on them) */
enum i915_lcd_observe {
	I915_LCD_OBS_COMMIT_BEGIN = I915_XD_OBSERVE_COMMIT_BEGIN,
	I915_LCD_OBS_UNDERRUN_ARM = I915_XD_OBSERVE_UNDERRUN_ARM,
	I915_LCD_OBS_PIPE_ENABLED = I915_XD_OBSERVE_PIPE_ENABLED,
	I915_LCD_OBS_PLANE_ARMED = I915_XD_OBSERVE_PLANE_ARMED,
	I915_LCD_OBS_PLANE_DISABLED = I915_XD_OBSERVE_PLANE_DISABLED,
	I915_LCD_OBS_UNDERRUN_DISARM = I915_XD_OBSERVE_UNDERRUN_DISARM,
	I915_LCD_OBS_PIPE_DISABLED = I915_XD_OBSERVE_PIPE_DISABLED,
	I915_LCD_OBS_COMMIT_END = I915_XD_OBSERVE_COMMIT_END,
	I915_LCD_OBS_NUM = I915_XD_OBSERVE_POINTS
};

/*
 * wait_reg results (Linux numbering, as the reference's callers compare them):
 *   0                    the condition held
 *   I915_LCD_ETIMEDOUT the device did not reach the condition in time
 *   I915_LCD_EIO       the time source / the wait primitive / MMIO access failed: NOT a timeout; the backend has
 * also reported it through i915_lcd_backend_fault() so it is the run's first anomaly
 */
#define I915_LCD_ETIMEDOUT (-110)
#define I915_LCD_EIO       (-5)

/*
 * The sink hooks of an AUX channel of its own and the backend of a
 * screen are the xd ones (xd-backend.h); the older code names them so.
 */
#define i915_lcd_aux_emit i915_xd_sink_hooks
#define i915_lcd_emit i915_xd_backend
/*
 * The state calculation (state.c).
 */
/*
 * LCD-A, first slice: from what the panel and the VBT report
 * (EDID, DPCD, VBT colour depth) to the values the display hardware will be
 * programmed with: the panel mode, the link configuration and its bandwidth check,
 * the link / data M/N pair and the combo PLL configuration words.
 *
 * Calculation only: nothing is written to hardware.  The arithmetic is the
 * reference's own (generated files in this directory); the comparison target is
 * what Linux programmed on the same machine.
 *
 * NOT computed yet (LCD-A, next slices): DDI buffer translation / voltage swing,
 * transcoder timing register words, plane + scanout layout, CDCLK / bandwidth /
 * DBUF / watermark requirements, the enable and disable state sequences.
 */

/*
 * The link parameters of one modeset: rate, lanes and the M/N values.
 */
struct i915_lcd_link {
	int sink_max_rate_khz, sink_max_lanes;    /* DPCD 0x001 / 0x002: the capability */
	int rate_khz, lanes;                      /* the selected configuration */
	int use_max_params;                       /* eDP < 1.4: the reference trains at the maximum */
	int bpp;                                  /* pipe bits per pixel */
	int required_kbps, available_kbps;        /* intel_dp_link_required / intel_dp_max_data_rate */
	uint32_t tu, data_m, data_n, link_m, link_n;
};

/*
 * The DKL PLL words of a Type-C port's link clock: what
 * icl_calc_mg_pll_state() computes for display version 12 and later, one
 * word per DKL register.
 */
struct i915_lcd_dkl_words {
	uint32_t refclkin_ctl;
	uint32_t clktop2_coreclkctl1;
	uint32_t clktop2_hsclkctl;
	uint32_t div0;
	uint32_t div1;
	uint32_t lf;
	uint32_t frac_lock;
	uint32_t ssc;
	uint32_t bias;
	uint32_t tdc_coldst_bias;
};

/*
 * The PLL state of one modeset.
 */
struct i915_lcd_pll {
	int ref_khz;                              /* display.dpll.ref_clks.nssc */
	uint32_t cfgcr0, cfgcr1, div0;            /* a combo PHY's DPLL */
	struct i915_lcd_dkl_words dkl;            /* a Type-C port's TC PLL (zero on a combo PHY) */
};

/*
 * The computed state of one modeset: mode, link and PLL.
 */
struct i915_lcd_state {
	struct i915_lcd_mode mode;
	struct i915_lcd_link link;
	struct i915_lcd_pll pll;
	unsigned notes;                           /* messages the reference text emitted */
};

/* one register write, in the order the reference's writer functions issue them */
struct i915_lcd_regwrite {
	uint32_t reg, value;          /* a write: the value.  A read-modify-write: the bits SET */
	uint32_t clear;               /* read-modify-write only: the bits cleared first */
	uint8_t rmw;                  /* 1 = read-modify-write (the resulting word depends on the hardware) */
	const char *step;             /* not a register operation: a reference callee that is not ported yet, at its position */
};
#define I915_LCD_MAX_REGWRITES 96u
/*
 * The register words one modeset writes, recorded in order.
 */
struct i915_lcd_words {
	unsigned n;
	unsigned overflow;
	struct i915_lcd_regwrite w[I915_LCD_MAX_REGWRITES];
};

/*
 * The commit observation (diagnostics.c).
 */
/*
 * Linux-derived -- what the LCD test LOOKS AT while the reference's modeset runs: the pipe's underrun status
 * (ICL_PIPESTATUS), its frame counter and its interrupt mask.  zedBSD project code; plain C over i915_lcd_ops,
 * so the same code runs on the register model and on the real GPU.
 *
 * Why it exists: this private, synchronous, one-buffer test does not connect the DRM vblank machinery nor the
 * underrun interrupt (see modeset-internal.h).  In exchange it OWNS the status register for the run:
 *   - the value found before the run is saved (not judged: it belongs to whoever ran before);
 *   - the status is cleared where the reference clears it (intel_set_cpu_fifo_underrun_reporting(true));
 *   - it is sampled at every observe() point of the commits and by the caller during the steady picture; bits seen
 *     are RECORDED first and only then cleared, so that a later period can be told from an earlier one and no
 *     occurrence is lost;
 *   - start / stop periods and the steady period are accumulated apart: an underrun while the pipe starts is a
 *     different statement from one while a picture stands.
 * The pipe's vblank interrupt must stay masked for the whole run (nothing here could service it): every sample also
 * reads GEN8_DE_PIPE_IMR and remembers if the vblank bit was ever found unmasked -- judged only while the pipe's
 * power well is held (the register lives in that well: before, it reads 0; the power-well enable programs the mask).
 * "Frames advance" is decided from the hardware frame counter alone -- never from elapsed time.
 */

#define I915_LCD_OBS_STEADY 100               /* sample points of the caller: the steady picture */
#define I915_LCD_OBS_MAX_SAMPLES 96u

/*
 * One sample the commit observer took at a named point of the commit.
 */
struct i915_lcd_obs_sample {
	int point;                      /* enum i915_lcd_observe, or I915_LCD_OBS_STEADY */
	uint32_t status;                /* ICL_PIPESTATUS as read */
	uint32_t frame;                 /* PIPE_FRMCOUNT_G4X as read */
	uint32_t imr;                   /* GEN8_DE_PIPE_IMR as read */
	int steady;
};

/*
 * The observer of a commit: the samples it took at the named points.
 */
struct i915_lcd_observer {
	struct i915_lcd_emit *hw;     /* the backend that is read (NOT the recorder: these reads are not part of the path) */
	int pipe;
	uint32_t status_reg, frame_reg, imr_reg;
	uint32_t underrun_mask;         /* the bits the reference treats as underrun status on this display version */
	uint32_t vblank_bit;
	uint32_t saved_before;          /* status found before the run */
	int begun, steady;
	int powered;                    /* between the enable's underrun-arm point and the disable's pipe-disabled point the crtc holds
	                                 * the pipe's power domain; outside, pipe A's registers (power well A) read 0 and say nothing */
	uint32_t seen_transition;       /* underrun bits seen while the pipe was started / stopped */
	uint32_t seen_steady;           /* underrun bits seen while the picture stood */
	int vblank_unmasked_seen;
	/* the stop evidence, taken at the disable's pipe-disabled point -- the last moment the pipe's power well is on */
	int stop_checked; uint32_t stop_transconf, stop_frame0, stop_frame1;
	unsigned n, dropped;
	struct i915_lcd_obs_sample s[I915_LCD_OBS_MAX_SAMPLES];
};

/* the registers the run log reads back, by the reference's names (diagnostics.c) */
struct i915_lcd_named_reg {
	const char *name;
	uint32_t reg;
	int has_linux;                  /* Linux's dump of this machine has a value for it */
	uint32_t linux_value, compare_mask;
};

       /* the reference's DBUF_CTL_S(slice), slice 0 = S1 */      /* pipe A / port A / DPLL 0; 0 = unknown name */

/*
 * The modeset trace (diagnostics.c).
 */
/*
 * a recorder stacked on any i915_lcd_ops backend (the register / sink model or the
 * real GPU): every operation is forwarded, and logged in order with what the backend answered.  It is
 * the run log of a modeset ("which phase, which write, what the wait returned, the first anomaly") and
 * the source of the sequence tables; it never replays anything.  zedBSD project code; plain C.
 */

enum i915_lcd_trace_kind {
	I915_LCD_T_WRITE = 1,     /* a = reg, b = value */
	I915_LCD_T_RMW,           /* a = reg, b = set, c = clear, d = old value */
	I915_LCD_T_READ,          /* a = reg, b = value, n = how many identical reads in a row (polling) */
	I915_LCD_T_WAIT,          /* a = reg, b = value wanted, c = mask, rc */
	I915_LCD_T_DPCD_WRITE,    /* a = offset, b = first bytes (little end first), c = size, rc */
	I915_LCD_T_DPCD_READ,     /* a = offset, b = first bytes, c = size, rc */
	I915_LCD_T_PANEL,         /* a = op, rc */
	I915_LCD_T_POWER_GET,     /* a = domain, rc = wakeref */
	I915_LCD_T_POWER_PUT,     /* a = domain, b = wakeref; asynchronous: d = 1, c = delay in ms */
	I915_LCD_T_STEP,          /* name: a reference callee that is not ported */
	I915_LCD_T_ERROR,         /* name: a drm_err / WARN of the reference text */
	I915_LCD_T_PHASE,         /* name: a marker written by the caller */
	I915_LCD_T_DECIDED,       /* name: a reference callee deliberately not connected (reason in modeset-internal.h) */
	I915_LCD_T_DBUF,          /* a = the DBUF slices requested (gen9_dbuf_slices_update) */
	I915_LCD_T_OBSERVE,       /* a = enum i915_lcd_observe: a point of the commit */
};

/*
 * One recorded operation of a modeset run.
 */
struct i915_lcd_trace_entry {
	uint8_t kind;
	uint16_t n;
	int32_t rc;
	uint32_t a, b, c, d;
	const char *name;
};

#define I915_LCD_TRACE_MAX 2048u
/*
 * The ordered record of what a modeset run did, stacked on the hooks it
 * records.
 */
struct i915_lcd_trace {
	struct i915_lcd_emit *backend;
	struct i915_lcd_emit ops;             /* hand THIS to the modeset */
	unsigned n, dropped;
	unsigned writes, rmws, waits, wait_timeouts, steps, decided, errors, sleeps;
	uint64_t slept_us;
	int first_error_at;                     /* index of the first ERROR entry, or -1 */
	void (*tap)(void *ctx, int point);      /* optional: called at every observe() point, before the backend */
	void *tap_ctx;
	struct i915_lcd_trace_entry e[I915_LCD_TRACE_MAX];
};


/*
 * What a screen holds and what a flip did are the pictures' records
 * (xd-picture.h); the older code names them and their results so.
 */
#define i915_lcd_modeset_status i915_xd_picture_status
#define i915_lcd_flip_result i915_xd_picture_flip

#define I915_LCD_MS_OK I915_XD_PICTURE_OK
#define I915_LCD_MS_NOT_PREPARED I915_XD_PICTURE_NOT_READY
#define I915_LCD_MS_ERRORS I915_XD_PICTURE_ERRORS
#define I915_LCD_MS_LINK_NOT_TRAINED I915_XD_PICTURE_LINK_NOT_TRAINED
#define I915_LCD_MS_STILL_OWNED I915_XD_PICTURE_STILL_OWNED

#define I915_LCD_FLIP_DONE I915_XD_PICTURE_FLIP_DONE
#define I915_LCD_FLIP_NOT_LATCHED I915_XD_PICTURE_FLIP_NOT_LATCHED
#define I915_LCD_FLIP_TIMEOUT I915_XD_PICTURE_FLIP_TIMEOUT
#define I915_LCD_FLIP_REFUSED I915_XD_PICTURE_FLIP_REFUSED
#define I915_LCD_FLIP_ARMED I915_XD_PICTURE_FLIP_ARMED

/*
 * The show of a resident run (resident.c): the buffer's life (create, pin,
 * draw, publish, begin ... end, unpin, destroy, or abandon), the light and
 * the dark of the screen, the observation between them, and the decision
 * whether the buffer may be given back.
 *
 * Three records are kept apart, because they answer different questions:
 * the first anomaly (the first thing that went wrong, where, and how far
 * the run had got, never overwritten by what the cleanup reports), the
 * state the light left (software state and what the sink and the frame
 * counter said), and what the dark and the stop confirmation returned and
 * what is still held.  Software flags ("the arm was written") and hardware
 * observation (frame counter, underrun status, sink link status) are not
 * mixed.
 */

enum i915_lcd_show_stage {
	I915_LCD_SHOW_NONE = 0,
	I915_LCD_SHOW_BUFFER_READY,           /* scanout pinned, picture drawn, published and read back */
	I915_LCD_SHOW_PREPARED,               /* the modeset's check phase accepted the state */
	I915_LCD_SHOW_ENABLE_RETURNED,        /* the enable commit returned (successfully or not) */
	I915_LCD_SHOW_PICTURE_UP,             /* enable OK and the frame counter advances */
	I915_LCD_SHOW_WINDOW_DONE,            /* the finite observation window passed */
	I915_LCD_SHOW_DISABLE_RETURNED,
	I915_LCD_SHOW_STOP_CONFIRMED,         /* pipe reported off AND the frame counter stands */
	I915_LCD_SHOW_RELEASED,               /* buffer unpinned and destroyed */
	I915_LCD_SHOW_ABANDONED               /* stop not confirmed: buffer and everything it needs kept for ever */
};

/*
 * What one show needs: the screen's configuration, the backend, the GPU
 * memory, the pipe it watches, and the run's window and stage hooks.
 */
struct i915_lcd_show_env {
	struct i915_lcd_emit *hw;             /* the backend */
	struct i915_gt_mem *gm;               /* where the scanout buffer lives */
	struct i915_xd_picture_config cfg;    /* everything but the framebuffer: filled by the caller */
	int pipe;
	unsigned first_frames_ms;               /* how long to wait for the first frames */
	unsigned window_ms;                     /* the finite window after the in-window work */
	/*
	 * Optional: the work that runs while the picture is up (after the first frames, before the window); 0 = done.
	 * A failure is recorded as the first anomaly (if none yet) and the dark follows.  at_stage is told every stage.
	 */
	int (*in_window)(void *ctx, struct i915_lcd_observer *o);
	void *in_window_ctx;
	void (*at_stage)(void *ctx, int stage);
	void *at_stage_ctx;
};

/*
 * What one picture on the panel did: stages, samples and what was given
 * back.
 */
struct i915_lcd_show_report {
	int output_hdmi;                        /* the run drove an HDMI sink: no DP link to judge it by */
	int stage;                              /* the furthest stage reached */
	/* first anomaly */
	const char *first_anomaly;              /* 0 = none */
	int first_anomaly_stage;
	int first_anomaly_rc;
	int first_error_trace_at;               /* index in the run log of the first reference error; -1 = none */
	/* return codes, in order */
	int window_rc, create_rc, pin_rc, prepare_rc, begin_rc, enable_rc, first_frames_rc, steady_rc, disable_rc, stopped_rc;
	int unpin_rc, destroy_rc;
	/* enabled state / cleanup result */
	struct i915_lcd_modeset_status at_enable, at_window_end, at_disable;
	unsigned enable_errors, cleanup_errors;
	int cleanup_first_error_trace_at;
	/* hardware observation */
	uint32_t frame_first, frame_last, steady_frame_first, steady_frame_last, stop_frame_first, stop_frame_last;
	unsigned steady_rounds;
	int window_hook_rc;
	uint32_t transconf_after_stop;
	/* the buffer */
	uint64_t surf;
	int released, abandoned;
	int display_acquired;                   /* the buffer was handed to the display (the enable commit was attempted) */
	int display_released;                   /* the display has provably stopped reading it (back to PINNED) */
	struct i915_lcd_observer obs;
	struct i915_lcd_trace *trace;         /* the run log (static storage of the show body) */
	int pass;
};

/*
 * The resident runs (resident.c, present.c, head.c).
 */

struct i915_edp_device;
struct i915_display_state;
struct i915_dmc_dev;
struct i915_irq_dev;
struct i915_display_irq;
struct i915_gt_engines;
struct i915_gt_ppgtt;
struct spinlock;

/* the release decision after a draw and (maybe) a show -- exported for the GPU-free test */
struct i915_fhd_render;
struct i915_scanout;
struct i915_gt_tlb;

/* what the runner reports next to the probe result (the probe outcome itself is not changed by the LCD test) */
struct i915_lcd_test_summary {
	int ran, pass;
	const char *stage;                      /* furthest stage */
	const char *first_anomaly, *first_anomaly_stage;
	int cleanup_rc;                         /* the disable commit's result */
	unsigned cleanup_errors;
	int retained;                           /* resources kept because the stop was not confirmed */
};

/*
 * The resident eDP device (dp-sink.c).
 */
/* the env's kernel backend: real mutexes, tick-driven sleeps, timer + worker threads */
struct i915_dp_kernel {
	struct i915_mmio *mmio;
	struct i915_power_domains *pd;
	struct i915_pw_ctx *pwc;
	struct mutex locks[2];                  /* I915_DP_LOCK_* */
	struct i915_xd_io aux_io;               /* the AUX channels' and the PPS's window on mmio, under the PPS mutex */
	struct i915_workqueue wq;            /* the display worker: VDD-off and async power put */
	struct i915_timer_queue tq;
	struct i915_delayed_work vdd_off_work;
	struct i915_delayed_work async_put_work;
	int sync_started;
	unsigned wait_timeouts, time_faults;
	unsigned tick_sleeps;                   /* ordinary sleeps: tick + wait queue (kern_usleep_range) */
	unsigned busy_sleeps;                   /* always 0 since no busy remainder is left */
	uint64_t tick_slept_us, busy_slept_us;
	uint64_t last_ms;
};

/*
 * The resident eDP panel: its AUX and PPS environment, the locks and the
 * delayed work, and what the bring-up found.
 */
struct i915_edp_device {
	int started;                            /* locks and threads exist */
	int connector_live;                     /* the normal initialisation succeeded; state retained */
	int init_rc, late_rc;
	struct i915_dp_kernel k;
	struct i915_dp_env env;
	struct i915_edp_config cfg;
	struct i915_edp_result res;           /* kept for the later display stages */
	struct i915_display_mode mode;        /* the panel's mode, from its EDID */
	struct i915_display_link link;        /* the link and the combo PLL the screen's plan chose for it */
	int link_rc;                          /* 0, or why the mode or the link could not be planned */
	uint32_t ddi_buf_ctl_readout;           /* DDI_BUF_CTL of the eDP port when the connector was initialised */
	int vbt_bpp;
	struct i915_vbt_state *vbt;
	struct i915_xd_edp xe;                /* the panel's sink (xd-edp.c): its sequencer, DPCD and EDID */
};

/*
 * The hotplug (hotplug.c).
 */
/*
 * Linux-derived -- the HDMI hotplug receive path, the zedBSD side of the generated reference text.
 * zedBSD project code.  No Linux types here: device.c, interrupts.c and the tests use this interface.
 *
 *   start   intel_hpd_init_early() + the connectors intel_setup_outputs() would have made (one per encoder: eDP-1,
 *           HDMI-A-1, DP-1, DP-2), each encoder's ->hotplug = intel_ddi_hotplug and ->connected =
 *           lpt_digital_port_connected / TC (the latter recorded as unported), then the IRQ entry is opened
 *   irq     from gen8_de_irq_handler(): the acked SDEIIR value -> icp_irq_handler() (IRQ context)
 *   stop    the IRQ entry is closed, in-flight entries drained, then intel_hpd_cancel_work()
 */

struct i915_hotplug;

#define I915_HPD_MAX_CONNECTORS 8u
#define I915_HPD_MAX_IRQ_RECORDS 64u
#define I915_HPD_MAX_HOTPLUG_RECORDS 64u

/*
 * The register model a test starts the hotplug path on instead of the MMIO
 * BAR.  Its layout belongs to the test build (tests/display/hpd-model.h);
 * production only passes the pointer on.
 */
struct i915_hpd_fake_regs;

/* the last EDID read over DDC (drm_edid_read_ddc) */
struct i915_hpd_edid_info {
	int rc;                         /* blocks read (>= 1) or a negative errno */
	unsigned blocks, extensions, product, serial, version, revision, digital, checksum;
	unsigned pixel_clock_khz, hactive, vactive;     /* the first detailed timing descriptor */
	unsigned reads, fails;
	char mfg[4];
};

/*
 * One hotplug interrupt as the hotplug path recorded it.
 */
struct i915_hpd_irq_record {
	uint64_t tick;
	uint32_t sde_iir;               /* the acked SDEIIR value handed to icp_irq_handler() */
	uint32_t shotplug_ddi;          /* what its rmw of SHOTPLUG_CTL_DDI read (0 when the DDI trigger was clear) */
	uint32_t event_bits_after;      /* display.hotplug.event_bits when the handler returned */
};

/* one encoder->hotplug() call of the hotplug work (the reference's "Connector %s (pin %i) received hotplug event") */
struct i915_hpd_hotplug_record {
	uint64_t tick;
	unsigned connector;
	int pin, retries;
	int old_status, new_status;     /* enum connector_status (1 connected, 2 disconnected, 3 unknown) */
	int state;                      /* enum intel_hotplug_state (0 UNCHANGED, 1 CHANGED, 2 RETRY) */
	int live;                       /* SDEISR & pch_hpd[pin] at the detection */
	int edid_rc;                    /* the EDID read of this detection: blocks read, or a negative errno */
	unsigned edid_blocks;
};

/*
 * What the hotplug path saw and did.
 */
/* One connector of the hotplug path as the display inventory sees it. */
#define I915_HPD_OUTPUT_OTHER	0U
#define I915_HPD_OUTPUT_EDP	1U
#define I915_HPD_OUTPUT_HDMI	2U
#define I915_HPD_OUTPUT_DP	3U

struct i915_hpd_output {
	unsigned kind;
	int port;
	int connected;
	uint64_t generation;
	const char *name;
	uint32_t width;
	uint32_t height;
	uint32_t refresh_millihz;
	uint32_t width_mm;
	uint32_t height_mm;
};

/* A connector's preferred mode as the topology last took it from its EDID (0 when none). */
struct i915_hpd_output_mode {
	uint32_t width;
	uint32_t height;
	uint32_t refresh_millihz;
	uint32_t width_mm;
	uint32_t height_mm;
};

struct i915_hpd_summary {
	int started, live;
	unsigned num_connectors;
	unsigned irq_entries, irq_dropped, ddi_triggers, gmbus_irqs;
	unsigned hotplug_works, digport_works, reenable_works, retries_armed;
	unsigned hpd_pulse_steps, hotplug_events, irq_setups, storms, warnings;
	unsigned irq_records, hotplug_records;
	unsigned to_connected, to_disconnected;     /* HDMI-A connector status transitions */
	int hdmi_connector;                         /* index, -1 = none */
	int hdmi_status;                            /* enum connector_status, now */
	unsigned long long hdmi_epoch;
};

struct i915_workqueue;

/* the sanitized encoder of the readout (N1): port = enum port, output_type = enum intel_output_type */
#define I915_OUTPUT_ANALOG  1
#define I915_OUTPUT_DP      7
#define I915_OUTPUT_EDP     8
#define I915_OUTPUT_DSI     9
#define I915_OUTPUT_DDI     10
#define I915_OUTPUT_HDMI    6
#define I915_OUTPUT_DP_MST  11

/* the worker queue, the backlight policy (acpi_video_get_backlight_type), the connectors */
#define I915_OPREGION_POLICY_VIDEO  1     /* acpi_backlight_video: BCLP requests are served */
#define I915_OPREGION_POLICY_VENDOR 2
#define I915_OPREGION_POLICY_NATIVE 3     /* acpi_backlight_native: the reference ignores ASLE backlight requests */

/*
 * The takeover report (takeover.c).
 */
struct i915_lcd_emit;

/*
 * The resident node (device.c, display.c).
 */
/*
 * Resident mode: the device start stays up and serves /dev/gpuN.
 *
 * The start does not return while it serves: it publishes the GPU node,
 * executes the requests the ops layer queues, and when told to stop it falls
 * into the device stop.  What the serving thread needs lives in the display
 * (struct i915_display), so nothing has to stay on the start's stack.
 */

struct i915_lcd_kernel_deps;

/*
 * What the resident node's serving thread needs: the device, the
 * registers, the GT memory and engines, and the panel.
 */
struct i915_resident_ctx {
	struct i915_device *device;
	struct i915_mmio *mmio;
	struct spinlock *uncore_lock;
	struct i915_gt_mem *gm;
	struct i915_gt_engines *es;
	const struct i915_lcd_kernel_deps *lcd;	/* the panel (0 = the node has no display) */
};

/*
 * one frame the GPU copies into the panel's back buffer.  The serving thread maps both panel buffers into
 * `vm` (once), asks `build` for a batch that writes the frame into the back buffer (dst_va is the buffer's address
 * in `vm`), runs it in `context` and flips.  No CPU touches the pixels.
 */
struct i915_context;
struct i915_ppgtt;
typedef int (*i915_shim_blit_fn)(void *ctx, uint64_t dst_va, uint32_t width, uint32_t height, uint32_t pitch,
	uint64_t *batch_va);

/*
 * The resident display operations (display.c).
 */
/*
 * the display operations of the resident node (display.c).
 */

struct drv_gpu_display_ops;
struct drv_gpu_scanout_ops;



/*
 * One run of the resident output or of the second output (resident.c).
 *
 * The run's parameters (display-internal.h) name its output; the rest is
 * what the run took (power references, events) and what it saw, kept so
 * the teardown gives back exactly what the run holds.
 */

struct i915_lcd_kernel {
	/* The parameters of the run; NULL before it starts. */
	const struct i915_lcd_run_params *p;

	/* The display the run belongs to. */
	struct i915_display *display;

	/* What the run drives. */
	const struct i915_lcd_kernel_deps *d;

	/* The two modeset locks (dpll, backlight): lcdb_locks of the display. */
	struct mutex *locks;

	/* The register, wait, panel, power and vblank hooks of the run. */
	struct i915_lcd_emit ops;

	/* Power references this run took and has not returned, per domain. */
	int power_refs[I915_PW_DOMAIN_NUM];

	/* What went wrong, counted. */
	unsigned power_get_failures, wait_timeouts, time_faults, unresolved_steps, decided, errors;

	/* Nonzero once the run is in its cleanup. */
	int phase_cleanup;

	/* The window of the run (ms). */
	unsigned window_ms;

	/* The interrupt and vblank event state of the run. */
	int irq_was_on, event_armed, event_pipe;
	uint32_t event_frame;
	unsigned vblank_sleeps, sleep_irq_off, events_cancelled;

	/* The two flip buffers and what the flips did. */
	struct i915_scanout *flip_a, *flip_b;
	unsigned flips_done;

	/* Set when the disable commit starts; errors are counted apart from then on. */
	int probe_watch;
	uint32_t probe_first_dsl;
	unsigned cleanup_errors;
};

/* The size of the VBT copy the parser reads (the largest VBT the provider accepts). */
#define I915_VBT_MAX 8192u

/* The size of the DMC firmware arena (the Linux DISPLAY_VER13_DMC_MAX_FW_SIZE). */
#define I915_DMC_ARENA_SIZE 0x20000u

/* The size of the VBT copy the firmware display check takes out of the OpRegion. */
#define I915_NATIVE_VBT_COPY_SIZE 16384u

/*
 * What binds the display's Type-C ports (tc.c) to the device (tc-kern.c).
 *
 * It lives in the display for the device's lifetime and is filled once the
 * display probe has made its encoders.  Each port's mutex is the lock tc.c
 * takes through its environment; it is taken before the power domains'
 * lock.  aux_ch is the AUX channel the VBT gives each port, whose AUX_USBC
 * domain blocks TC cold.  live is zero on a display without Type-C ports,
 * and then nothing here is used.
 */
struct i915_tc_kern {
	/* The Type-C core and its ports. */
	struct i915_tc tc;

	/* Each port's lock. */
	struct mutex locks[I915_TC_PORTS];

	/* Each port's AUX channel (enum aux_ch). */
	int aux_ch[I915_TC_PORTS];

	/* The registers and the power the ports use. */
	struct i915_mmio *mmio;
	struct i915_power_domains *pd;
	struct i915_pw_ctx *pwc;

	/* The display version the domains are named for. */
	unsigned display_ver;

	/* How many power gets of the ports failed (diagnostics). */
	unsigned power_failures;

	/* Nonzero once the ports are bound. */
	int live;
};

/*
 * The display part of one device.
 *
 * The device owns exactly one; the device start fills it in the order of
 * the Linux display probe and the device stop takes it apart in reverse.
 * It holds every long-lived display object: the former static locals of
 * the probe, the former file-scope state of the display files, and one
 * pointer per Linux environment to that environment's own state (whose
 * types are complete only inside the environment, so the owner file of
 * the environment allocates it).
 *
 * Unless a field says otherwise it is written by the device start or stop
 * only, which run on the start worker one at a time; after the start the
 * resident serving thread is the only other writer.
 */

/*
 * The kinds of output the resident run lights: the built-in
 * eDP panel, an HDMI display, or an external DisplayPort display (a Type-C
 * port's, which fills its part).  Zero is the panel.
 */
enum i915_output_kind {
	I915_OUTPUT_KIND_PANEL = 0,
	I915_OUTPUT_KIND_HDMI,
	I915_OUTPUT_KIND_DP_EXT,
	I915_OUTPUT_KIND_COUNT
};

/*
 * The output the resident node drives (output.c).
 *
 * Chosen when the node's panel dependencies are filled at the device start:
 * the firmware's output (the GPU scanout rule).  Keiland moves it to another
 * connected output while no lease is held (display.c's claim),
 * and the end of such an output's last hold brings the firmware's back.
 * Zeroed, it is the eDP panel of an unknown connector.
 */
struct i915_display_output {
	/* What it is: the panel, an HDMI display (DDI B on pipe B, DVI mode), an external DP display. */
	enum i915_output_kind kind;

	/* Nonzero: the node drives no output (the firmware's output could not be driven). */
	int none;

	/* Nonzero once connector names the hotplug path's connector of the output (its ID and generation follow it). */
	int has_connector;
	unsigned connector;

	/* The HDMI or DP mode (its physical size included, 0 when unknown), and the link and PLL its plan chose. */
	struct i915_display_mode mode;
	struct i915_display_link link;

	/*
	 * An external DP display's Type-C port (0 = TC1) and DDI port, and
	 * what its sink's probe found when the output was prepared;
	 * unused for the other kinds.
	 */
	unsigned tc_port;
	int port;
	struct i915_dp_ext_sink sink;

	/* Where the mode came from, for the log: "EDID", "display.mode", "CEA 4". */
	const char *mode_source;
};

/*
 * The second output, the head:
 * an HDMI or an external DisplayPort display shown beside the resident
 * output while Keiland holds both leases.
 *
 * The claim (display.c) gives the head its lease and prepares its output;
 * the head's first frame lights it on modeset screen 1, from inside the
 * display window the resident output keeps; its release, and the window's
 * end, stop it.  The lease fields are guarded by the head's own mutex, so
 * that a frame waiting for the head's flip never holds up the resident
 * output's.  The worker reads claimed and pipe under the device IRQ lock
 * when it decides whether the resident output is lit again for two pipes;
 * everything else of the lit head is the worker's alone.
 */
struct i915_display_head {
	/* Serializes the lease, the sequence and the claim; inited once the mutex is. */
	struct mutex mutex;
	int inited;

	/* The session holding the head's lease, NULL while there is none, and the lease (numbered with the resident output's). */
	void *owner;
	uint64_t lease;

	/* Completed presentations of the lease, and the tick of the last one. */
	uint64_t sequence;
	uint64_t present_tick;

	/* Nonzero while the head is claimed; the connector, its generation at the claim, and the head's pipe. */
	int claimed;
	unsigned connector;
	uint64_t generation;
	unsigned pipe;

	/* The output the claim prepared (its mode, link and PLL); the worker lights it. */
	struct i915_display_output output;

	/* Nonzero while the head is lit (the worker writes it). */
	int up;

	/* The head's two buffers at its mode's size, the one it shows, and what the presenting space maps of them. */
	struct i915_scanout buf[2];
	unsigned front;
	struct i915_ppgtt *map_vm;
	uint64_t map_va[2];
	unsigned map_pages[2];

	/* The head's run: its hooks, its vblank event, its power references, the parameters its pipe is read from, its configuration (kept off the stack). */
	struct i915_lcd_kernel k;
	struct i915_lcd_run_params params;
	struct i915_xd_picture_config cfg;

	/*
	 * A connector that could not be lit beside the resident output, and the
	 * connector's generation it was latched at (D-LIMIT): its claims are the
	 * limit until it is plugged again.  The device IRQ lock guards all three.
	 */
	int limited;
	unsigned limited_connector;
	uint64_t limited_generation;

	/*
	 * Nonzero while the lit head's buffers, and its last picture, are kept
	 * over a display window's end (a sleep, a hold that ran out): the next
	 * window lights the head again with them at its start.
	 * Only the worker reads and writes it.
	 */
	int dormant;

	/* Nonzero once a head's stop was not confirmed: its buffers are abandoned and no head is lit again. */
	int broken;
};

struct i915_lcd_world;
struct kern_backlight;
struct i915_wm_world;
struct i915_dp_world;
struct i915_vbt_world;
struct i915_hpd_world;

struct i915_display {
	/* The device this display belongs to. */
	struct i915_device *device;

	/* The xd display: its register window, the device's description and what its parts found (xd.h). */
	struct i915_xd xd;

	/*
	 * Probe state (the probe's static locals, device lifetime).
	 */

	/* The DRM device management and the per-pipe vblank workers (drm_dev_init, drm_vblank_init). */
	struct i915_drm_device drm_dev;

	/* The VBT the parser was given and what it found (intel_bios_init). */
	struct i915_vbt_state vbt_state;

	/* The VGA arbiter client (intel_vga_register). */
	struct i915_vga_client vga_client;

	/* The power wells and domains; its own lock protects the reference counts. */
	struct i915_power_domains power_domains;

	/* The PM demand lock and wait queue (intel_pmdemand_init_early). */
	struct i915_pmdemand pmdemand;

	/* The CDCLK state and hooks. */
	struct i915_cdclk_dev cdclk;

	/* The context the power-well operations run in (MMIO, interrupt hooks, DMC). */
	struct i915_pw_ctx pwc;

	/* The resident eDP panel: outlives the output setup, released in the stop. */
	struct i915_edp_device edp_dev;

	/* The display core (DBUF slices, the init power reference). */
	struct i915_display_core dcore;

	/* The DMC loader; its work runs on dmc_wq. */
	struct i915_dmc_dev dmc_dev;

	/* The display software state (mode config, global objects, FBC). */
	struct i915_display_state dstate;

	/* The PCH the display found (intel_detect_pch). */
	struct i915_pch_state pch;

	/* The crtc, plane, DPLL and encoder records the output setup and the readout build. */
	struct i915_display_nogem nogem;

	/* The Linux driver-probe state after the noirq stage (hotplug, registration, IPC). */
	struct i915_driver_probe dprobe;

	/* The DRAM the bandwidth code was given, and the bandwidth state (intel_bw_init_hw). */
	struct i915_dram_info dram_info;
	struct i915_bw_state bw_state;

	/* The display half of the interrupt device and its vblank delivery. */
	struct i915_display_irq irq;
	struct i915_irq_vblank irq_vblank;

	/* The DMC, modeset and flip workqueues (intel_modeset_wq, intel_flip_wq). */
	struct i915_workqueue dmc_wq, modeset_wq, flip_wq;

	/* The OpRegion data read at the start (read-only acquisition); p2_opd points at opd or is NULL. */
	struct i915_opregion_data opd;
	const struct i915_opregion_data *p2_opd;

	/* Nonzero when the start found an OpRegion, and one with a usable VBT. */
	int opregion_present;
	int opregion_vbt_present;

	/* The firmware's output read from the firmware check's record, the only one the driver lights by itself. */
	struct i915_gop_output gop;

	/* The Type-C ports and what binds them to this display (tc-kern.c). */
	struct i915_tc_kern tck;

	/* Nonzero once the display core was initialized (intel_power_domains_init_hw). */
	int display_core_inited;

	/* Nonzero while the hotplug path runs (started after the display probe). */
	int hpd_started;

	/* The resident run: what the serving thread needs, and the panel dependencies it uses. */
	struct i915_resident_ctx rctx;
	struct i915_lcd_kernel_deps rlcd;

	/*
	 * The output the resident run lights: the panel, or another output;
	 * and the firmware's, which the start chose and which
	 * comes back when a moved output's last hold ends.  The
	 * device IRQ lock covers a change of output against the worker, which
	 * reads it only when it enters the display window.
	 */
	struct i915_display_output output;
	struct i915_display_output gop_output;

	/* The panel's backlight device while the node is published with the panel as its output (backlight.c), or NULL. */
	struct kern_backlight *backlight;

	/* The resident output's refresh boundaries (control.c). */
	struct i915_display_refresh refresh;

	/*
	 * The resident present path.
	 */

	/* The display lease; its own mutex protects it. */
	struct i915_resident_display rd;

	/* The serving thread's side of the display window. */
	struct i915_present_window window;

	/* The second output. */
	struct i915_display_head head;

	/* The panel run of the resident path; written by the serving thread only. */
	struct i915_lcd_kernel lk;

	/* The dpll and backlight locks of the panel runs; lcdb_locks_live once initialized. */
	struct mutex lcdb_locks[2];
	int lcdb_locks_live;

	/*
	 * The two full-panel buffers of the resident path.  They outlive a run
	 * whose stop was not confirmed; the serving thread flips between them.
	 */
	struct i915_scanout resident_buf[2];

	/* The serving loop the display window calls back, and its context. */
	int (*resident_serve)(void *ctx);
	void *resident_serve_ctx;

	/* Which resident buffer is on the panel, and whether the panel is up. */
	unsigned resident_front;
	int resident_up;

	/* The show environment and report of the resident run (large, kept off the stack). */
	struct i915_lcd_show_env resident_run_env;
	struct i915_lcd_show_report resident_run_rep;

	/* The panel data the run configuration was filled from (kept off the stack). */
	struct i915_vbt_panel fill_cfg_pn;

	/* Nonzero logs every register access of a panel run (a diagnostic switch). */
	int i915_lcd_reg_trace;

	/*
	 * What a run kept because its stop was not confirmed: the modeset
	 * trace, the hooks of the hardware left running, and the GPU buffer the
	 * GPU was not shown to be done with.  The stop reads them to decide
	 * what it may release.
	 */
	struct i915_lcd_trace show_trace;
	int show_retained;
	const struct i915_lcd_emit *show_retained_hw;
	int show_gpu_retained;
	const void *show_gpu_retained_gm;
	const char *show_gpu_retained_why;

	/*
	 * Former file-scope state of the world-neutral display files.
	 */

	/* The VBT bytes the parser reads, the OpRegion's VBT, and the pinned VBT's PCI ids (vbt.c; the ids only in the test build). */
	uint8_t i915_vbt_buf[I915_VBT_MAX];
	const void *opregion_vbt_buf;
	size_t opregion_vbt_size;
	uint16_t pinned_vendor, pinned_device;

	/* The DMC firmware image as loaded, and how much of it is used (dmc.c). */
	uint8_t g_dmc_arena[I915_DMC_ARENA_SIZE];
	unsigned g_dmc_arena_used;

	/* The display core's own trace (power.c). */
	struct i915_trace dc_trace;

	/* The OpRegion and VBT copies of the OpRegion read. */
	uint8_t i915_opregion_read_data_opcopy[I915_OPREGION_SIZE];
	uint8_t i915_opregion_read_data_vbtcopy[I915_NATIVE_VBT_COPY_SIZE];

	/* The ACPI notifier chain of the OpRegion service; chain_lock protects chain_head (opregion.c). */
	struct mutex chain_lock;
	int chain_lock_live;
	struct notifier_block *chain_head;

	/* The last ISA bridge the PCH search looked at (device-info.c). */
	struct drv_pci_device *next_isa_bridge_cursor;

	/* The VGA I/O port operations (takeover.c); NULL until the VGA part is bound. */
	const struct i915_vga_io_ops *g_vga_io;

	/*
	 * The Linux environments' own state.  Each is allocated and freed by
	 * the owner file of its environment and is NULL outside that window.
	 */
	struct i915_lcd_world *lcd_world;
	struct i915_wm_world *wm_world;
	struct i915_dp_world *dp_world;
	struct i915_vbt_world *vbt_world;
	struct i915_hpd_world *hpd_world;

	/*
	 * The stages of the display start that completed, so the stop takes
	 * apart exactly what the start built (the probe's local flags).
	 */
	int drm_inited;
	int vga_registered;
	int power_domains_inited;
	int dmc_inited;

	/* DC9 was entered for a suspend and is to be left by the resume. */
	int suspend_dc9;
	int modeset_wq_ok;
	int flip_wq_ok;
	int dstate_inited;
	int nogem_inited;

	/* Nonzero while the display half is bound to the interrupt device. */
	int irq_bound;

	/*
	 * Nonzero once the firmware display check decided the display is not
	 * to be used: every later display stage is skipped and the node has no
	 * panel.
	 */
	int absent;
};

#endif
