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
 * The Linux display code left is the VBT parser's, compiled in its own
 * environment, a private header (vbt.h).
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
#include "xd-dp.h"
#include "xd.h"
#include "xd-backend.h"
#include "xd-edp.h"

/*
 * The old name of struct i915_display_mode (display-internal.h), which
 * host tests written against the older display code still spell.
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
 * and defines I915_DISPLAY_WORLD_<NAME> for a header layered on it.
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
 * The port numbers the hotplug, Type-C and external DisplayPort code use:
 * the combo ports A and B, and the first Type-C port, which is also port
 * D's number.
 */
#define I915_PORT_A      0
#define I915_PORT_D      3
#define I915_PORT_TC1    3

/*
 * The QGV points the display lets SAGV use, as the start's forced SAGV
 * disable left them; the resident run reads the one allowed point's
 * bandwidth from them.
 */
struct i915_bw_obj_state {
	uint16_t qgv_points_mask;
};

/*
 * The bandwidth state the start of the display keeps: the QGV points and
 * what the forced SAGV disable asked the PCODE and got back.  Filled once
 * by the start, read by the resident run and the start's log.
 */
struct i915_display_state {
	struct i915_bw_obj_state bw_obj_state;
	int sagv_force_disable_attempted;
	int sagv_pcode_ret;             /* the PCODE's answer to the restriction */
	unsigned sagv_qgv_points;       /* the QGV points the restriction kept */
	unsigned sagv_psf_points;       /* the PSF GV points the restriction kept */
};

/*
 * The Linux driver probe (display.c, power.c).
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
 *     the hotplug pins                   armed by hpd.c (xd-hpd.c)
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

/* The display arbiter control and its isochronous priority control enable. */
#define I915_DISP_ARB_CTL2          0x45004u
#define I915_DISP_IPC_ENABLE        (1u << 3)

/*
 * What the Linux display driver probe leaves after its noirq stage:
 * hotplug, registrations and the IPC setting.
 */
struct i915_driver_probe {
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
#define I915_GOP_HDMI		2U	/* a combo PHY port in HDMI or DVI mode: the driver's HDMI path */
#define I915_GOP_OTHER		3U	/* an interface this driver cannot light yet */
#define I915_GOP_DP_TC		4U	/* another port of the platform in DP SST mode: the external DP path */

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
 * The built-in panel's device (edp-device.c): the kernel backend it runs
 * on (struct i915_dp_env, bound to the MMIO, the waits, the power domains,
 * the locks and the delayed work), its configuration and what its start
 * found.
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

#define I915_EDP_MAX_EDID_BLOCKS 4u

/*
 * Result codes are the reference's negative errnos in LINUX numbering (zedBSD's
 * <errno.h> numbers differ): rc == -I915_EDP_ETIMEDOUT and so on.
 */
#define I915_EDP_EIO 5
#define I915_EDP_ENXIO 6
#define I915_EDP_EBUSY 16
#define I915_EDP_EINVAL 22
#define I915_EDP_ETIMEDOUT 110

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
/* panel power sequencer / backlight-power operations, executed by the resident eDP (edp-device.c) */
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

/*
 * The resident eDP device (edp-device.c).
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

struct i915_workqueue;

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
	/* What it is: the panel, an HDMI display (on a combo PHY port, DVI mode), an external DP display. */
	enum i915_output_kind kind;

	/* The pipe and transcoder it is lit on. */
	unsigned pipe;

	/* Nonzero: the node drives no output (the firmware's output could not be driven). */
	int none;

	/* Nonzero once connector names the hotplug path's connector of the output (its ID and generation follow it). */
	int has_connector;
	unsigned connector;

	/* The HDMI or DP mode (its physical size included, 0 when unknown), and the link and PLL its plan chose. */
	struct i915_display_mode mode;
	struct i915_display_link link;

	/*
	 * The DDI port of an HDMI or an external DP display; an external DP
	 * display's Type-C port (0 = TC1) and what its sink's probe found
	 * when the output was prepared.  Unused for the panel.
	 */
	unsigned tc_port;
	int port;
	struct i915_xd_dp_probe sink;

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
struct i915_dp_ext_world;
struct i915_tc_port_world;
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

	/* The bandwidth state the start left (the QGV points SAGV may use). */
	struct i915_display_state dstate;

	/* The PCH the display found (intel_detect_pch). */
	struct i915_pch_state pch;

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
	struct i915_dp_ext_world *dp_ext;
	struct i915_tc_port_world *tc_ports;
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

	/* The front of the start after the interrupts ran: the stop takes the panel's device apart. */
	int front_done;

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
