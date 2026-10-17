/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The one-screen modeset, the one picture on the panel and the resident
 * panel run.
 *
 * The modeset object of a screen is built from the values an atomic check
 * would have computed (the state calculation, state.c) and driven by the
 * reference's own callers and callees in the other modeset files: prepare
 * runs the check phase, the two commits are intel_atomic_commit_tail()
 * reduced to one crtc, and status reads back what the run left behind.  It
 * is not an atomic-commit framework: one crtc, one encoder on a combo-PHY
 * port, one shared DPLL, one primary plane on one linear XRGB8888
 * framebuffer.  "The call returned" is never reported as "it worked".
 *
 * The show body puts one buffer on the panel through the two commits,
 * observes the picture, stops it through the reference's stop path and
 * decides whether the display has let go of the buffer.  Three records are
 * kept apart: the first anomaly, the state the enable left, and what the
 * cleanup returned.
 *
 * The resident run is the panel as the display of the resident node: two
 * full-panel buffers, one modeset, synchronous flips driven by
 * presentations, and the reference's stop path when the lease ends.  Its
 * hooks (registers, waits, panel, power, vblank, locks) work on the objects
 * the normal initialisation built; none is copied.
 *
 * XXX: drv_i915_lcd_display_ver(), drv_i915_lcd_backend_fault() and
 * drv_i915_lcd_debug() take no world, as their callers in the Linux text
 * have none; they reach the one bound world through a file-scope pointer.
 */

#include "modeset-internal.h"
#include "xd-pipe.h"
#include "xd-screen.h"
#include "watermark-internal.h"
#include "modeset.h"
#include "aux.h"
#include "clock.h"
#include "ddi.h"
#include "diagnostics.h"
#include "dp.h"
#include "dp-sink.h"
#include "dp-ext-kern.h"
#include "edid.h"
#include "head.h"
#include "output.h"
#include "panel.h"
#include "pipe.h"
#include "plane.h"
#include "power.h"
#include "tc-kern.h"
#include "present.h"
#include "scanout.h"
#include "state.h"
#include "takeover.h"
#include "vblank.h"
#include "vbt-parse.h"
#include "watermark.h"
#include <kern/kcrt.h>

#include "../intel/display-regs-xd.h"
#include "../i915.h"
#include "../ggtt.h"
#include "../memory.h"
#include "../mmio.h"
#include "../sync.h"

#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/irq.h>

#include <uapi/errno.h>
#include <stddef.h>

/* The display version the platform predicates answer before the probe sets one. */
#define I915_LCD_DEFAULT_DISPLAY_VER	13

/* DDI_BUF_PORT_REVERSAL and DDI_A_4_LANES of the DDI_BUF_CTL readout at connector init. */
#define I915_LCD_SAVED_PORT_BITS	((1U << 16) | (1U << 4))

/* How long a panel run waits for the first frames. */
#define I915_LCD_FIRST_FRAMES_MS	1000U

/* The first frames a run must see, and the frames each steady round must see within 1.5 s. */
#define I915_LCD_FIRST_FRAMES		10U
#define I915_LCD_STEADY_FRAMES		30U
#define I915_LCD_STEADY_ROUND_MS	1500U
#define I915_LCD_STEADY_STEP_MS		500U

/* How long the stop confirmation watches the frame counter. */
#define I915_LCD_STOP_WATCH_MS		200U

/* The DBUF geometry of Tiger Lake (two slices) and XE_LPD (four slices). */
#define I915_LCD_DBUF_TGL_SLICES	0x03U
#define I915_LCD_DBUF_TGL_BLOCKS	2048U
#define I915_LCD_DBUF_XELPD_BLOCKS	4096U
#define I915_LCD_DBUF_XELPD_SLICES	0x0fU

/* DDI_BUF_CTL of port B, whose saved bits an HDMI run on it keeps (intel_ddi_init()). */
#define I915_LCD_DDI_BUF_CTL_PORT_B	0x64100U

/* The pipe A registers the preflight reads raw on the HDMI arm. */
#define I915_LCD_TRANSCONF_B		0x71008U
#define I915_LCD_DDI_BUF_CTL_B		0x64100U
#define I915_LCD_SDEISR			0xc4000U
#define I915_LCD_DPLL0_ENABLE		0x46010U
#define I915_LCD_DPLL1_ENABLE		0x46014U

/* DDI_BUF_CTL of a port: port A's, and the distance between two ports (an external DP run's preflight). */
#define I915_LCD_DDI_BUF_CTL_A		0x64000U
#define I915_LCD_DDI_BUF_CTL_STRIDE	0x100U

/* The enable bit of a transcoder, a plane, a PLL and a DDI buffer, and the transcoder's state bit. */
#define I915_LCD_ENABLE_BIT		0x80000000U
#define I915_LCD_TRANSCONF_ON_MASK	0xc0000000U

/* The pipe A mask bits that must stay masked: vblank and FIFO underrun, plus the XE_LPD soft and hard underrun. */
#define I915_LCD_KEEP_MASKED_XELPD	0x80600001U
#define I915_LCD_KEEP_MASKED_GEN12	0x80000001U

/* The scanline register the evasion probe watches. */
#define I915_LCD_PIPEDSL_A		0x70000U
#define I915_LCD_PIPEDSL_MASK		0x1fffU

/*
 * How the resident run lights an output of one kind other than the panel: its
 * run parameters, its encoder's configuration over the filled panel
 * configuration, and whether it owns the PLL pool and the DBUF state from
 * their empty start (the only screen).  The panel needs none of them: the
 * run's defaults are the panel's.
 */
struct i915_resident_output_way {
	void (*params)(const struct i915_display_output *output, struct i915_lcd_run_params *params);
	void (*cfg)(struct i915_display *display, const struct i915_lcd_kernel_deps *d, const struct i915_display_output *output, struct i915_lcd_modeset_cfg *cfg);
	int own_pool;
};

/*
 * The modeset world the Linux text's world-less hooks report to.
 *
 * Set by the world's creation and cleared by its destruction; one display
 * binds it at a time.  NULL means no display has a modeset world.
 */
static struct i915_lcd_world *i915_modeset_bound_world;

/*
 * The display of the bound modeset world: its pictures hear the errors
 * the older code reports outside a screen's hooks.  Set and cleared with
 * the bound world.
 */
static struct i915_display *i915_modeset_bound_display;

static uint32_t i915_lcd_xd_window_read(void *context, uint32_t offset);
static void i915_lcd_xd_window_write(void *context, uint32_t offset, uint32_t value);
static void i915_lcd_xd_window_wait(void *context, unsigned microseconds);
static void i915_modeset_on_error(void *ctx, const char *what);
static void i915_show_anomaly(struct i915_lcd_show_report *r, const char *what, int rc);
static void i915_show_reached(struct i915_lcd_show_env *env, struct i915_lcd_show_report *r, int stage);
static int i915_show_first_error_after(const struct i915_lcd_trace *trace, unsigned from);
static int i915_show_display(struct i915_display *display, struct i915_lcd_show_env *env, struct i915_scanout *so, struct i915_lcd_show_report *r, uint32_t (*verify)(void *ctx, const struct i915_scanout *so), void *verify_ctx);
static int i915_show_passed(struct i915_display *display, const struct i915_lcd_show_report *r, int released);
static int i915_show_begin(struct i915_display *display, struct i915_lcd_show_env *env, struct i915_lcd_show_report *r);
static struct i915_display *i915_kernel_display(void *ctx);
static uint32_t i915_kernel_read32(void *ctx, uint32_t reg);
static void i915_kernel_write32(void *ctx, uint32_t reg, uint32_t value);
static uint32_t i915_kernel_rmw32(void *ctx, uint32_t reg, uint32_t clear, uint32_t set);
static void i915_kernel_posting_read(void *ctx, uint32_t reg);
static int i915_kernel_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value, unsigned timeout_ms);
static void i915_kernel_usleep(void *ctx, unsigned us);
static void i915_kernel_udelay(void *ctx, unsigned us);
static void i915_kernel_dbuf_slices_update(void *ctx, unsigned req_slices);
static void i915_kernel_lock(void *ctx, int which, int take);
static void i915_kernel_irq_off(void *ctx);
static void i915_kernel_irq_on(void *ctx);
static int i915_kernel_preflight_hdmi(struct i915_lcd_kernel *k);
static int i915_resident_window(void *ctx, struct i915_lcd_observer *o);
static uint32_t i915_resident_verify(void *ctx, const struct i915_scanout *so);
static int i915_resident_buffers(struct i915_display *display, const struct i915_lcd_kernel_deps *d, const struct i915_lcd_state *lcd);
static int i915_resident_buffers_kept(struct i915_display *display);
static void i915_resident_hdmi_params(const struct i915_display_output *output, struct i915_lcd_run_params *params);
static int i915_resident_takeover(struct i915_display *display, struct i915_lcd_kernel *k);
static void i915_resident_hdmi_cfg(struct i915_display *display, const struct i915_lcd_kernel_deps *d, const struct i915_display_output *output, struct i915_lcd_modeset_cfg *cfg);
static int i915_resident_release(struct i915_display *display, const struct i915_lcd_show_report *rep);
static int i915_resident_passed(const struct i915_lcd_show_report *rep);
static int i915_resident_output_way(enum i915_output_kind kind, struct i915_resident_output_way *way);
static void i915_resident_dp_ext_params(const struct i915_display_output *output, struct i915_lcd_run_params *params);
static void i915_resident_dp_ext_cfg(struct i915_display *display, const struct i915_lcd_kernel_deps *d, const struct i915_display_output *output, struct i915_lcd_modeset_cfg *cfg);
static int i915_kernel_preflight_dp_ext(struct i915_lcd_kernel *k);
static int i915_resident_dp_ext_fallback(struct i915_display *display, struct i915_display_output *output, int enable_rc);

/*
 * Allocates the modeset world of a display.
 *
 * The world starts zeroed except for the display version, which starts at
 * 13 until the probe sets the device's.  Returns 0, ENOMEM, or EBUSY when
 * another display already has a modeset world.
 */
int
drv_i915_lcd_world_create(
	struct i915_display *display)
{
	struct i915_lcd_world *world;

	/* One display binds the world-less hooks at a time. */
	if (i915_modeset_bound_world != NULL)
		return EBUSY;

	/* Allocates the zeroed world. */
	world = kern_calloc(1U, sizeof(*world));
	if (world == NULL)
		return ENOMEM;

	/* The display version the platform predicates answer until the probe sets one. */
	world->i915_lcd_ver = I915_LCD_DEFAULT_DISPLAY_VER;

	/* The display owns the world, and the world-less hooks report to it and to its pictures. */
	display->lcd_world = world;
	i915_modeset_bound_world = world;
	i915_modeset_bound_display = display;
	drv_i915_lcd_error_bind(world, i915_modeset_on_error, display);

	/* Succeeded: the modeset path has its state. */
	return 0;
}

/*
 * Frees the modeset world of a display.
 */
void
drv_i915_lcd_world_destroy(
	struct i915_display *display)
{
	/* A display without a world has nothing to free. */
	if (display->lcd_world == NULL)
		return;

	/* The world-less hooks no longer report to this world. */
	if (i915_modeset_bound_world == display->lcd_world) {
		drv_i915_lcd_error_bind(NULL, NULL, NULL);
		i915_modeset_bound_world = NULL;
		i915_modeset_bound_display = NULL;
	}

	kern_free(display->lcd_world);
	display->lcd_world = NULL;
}

/*
 * Converts a run's state and configuration of an output into the
 * configuration a screen is prepared from (xd-picture.h).
 *
 * Both an HDMI and an external DisplayPort sink, or a negative number,
 * give a configuration the prepare refuses.
 */
void
drv_i915_lcd_picture_config(
	const struct i915_lcd_state *s,
	const struct i915_lcd_modeset_cfg *cfg,
	struct i915_xd_picture_config *config)
{
	unsigned index;

	/* Starts empty. */
	kern_memset(config, 0, sizeof(*config));

	/* The kind of the output. */
	config->kind = I915_XD_SCREEN_PANEL;
	if (cfg->output_hdmi)
		config->kind = I915_XD_SCREEN_HDMI;
	if (cfg->output_dp_ext) {
		config->kind = I915_XD_SCREEN_DP;
		if (cfg->output_hdmi)
			config->kind = 0U;
	}

	/* The port, the pipe, the transcoder, the AUX channel and the PLL the caller expects. */
	config->ddi = (unsigned)cfg->port;
	config->pipe = (unsigned)cfg->pipe;
	config->trans = (unsigned)cfg->cpu_transcoder;
	config->aux = (unsigned)cfg->aux_ch;
	config->pll = (unsigned)cfg->dpll_id;
	config->also_pipes = (uint8_t)cfg->also_active_pipes;
	config->hdmi_level = cfg->vbt_hdmi_level_shift;
	config->kept = cfg->saved_port_bits;
	config->sink = cfg->aux_emit;

	/* The sink's capabilities and the panel's VBT data. */
	kern_memcpy(config->caps, cfg->dpcd, sizeof(config->caps));
	kern_memcpy(config->edp_caps, cfg->edp_dpcd, sizeof(config->edp_caps));
	config->vbt_low_vswing = cfg->vbt_low_vswing;
	config->vbt_hobl = cfg->vbt_hobl;
	config->vbt_backlight_present = cfg->vbt_backlight_present;
	config->vbt_backlight_active_low = cfg->vbt_backlight_active_low;
	config->vbt_backlight_controller = cfg->vbt_backlight_controller;
	config->vbt_backlight_hz = cfg->vbt_backlight_pwm_freq_hz;
	config->vbt_backlight_min = cfg->vbt_backlight_min_brightness;
	config->rawclk_khz = cfg->rawclk_khz;

	/* The memory's watermarks and the display buffer as the start found them. */
	for (index = 0U; index < 8U; index++)
		config->wm_latency[index] = cfg->wm_latency[index];
	config->wm_levels = cfg->wm_num_levels;
	config->wm_ipc = cfg->wm_ipc_enabled;
	config->sagv_block_us = cfg->sagv_block_time_us;
	config->dbuf_size = cfg->dbuf_size;
	config->dbuf_slice_mask = cfg->dbuf_slice_mask;
	config->dbuf_slices_now = cfg->dbuf_enabled_slices;
	config->mbus_joined_now = cfg->mbus_joined;

	/* The CD clock and its limit, the memory bandwidth, the DMC programs and the AFC override. */
	config->cdclk.ref_khz = cfg->cdclk_ref_khz;
	config->cdclk.vco_khz = cfg->cdclk_vco_khz;
	config->cdclk.cdclk_khz = cfg->cdclk_khz;
	config->cdclk.bypass_khz = cfg->cdclk_bypass_khz;
	config->cdclk.voltage = cfg->cdclk_voltage_level;
	config->cdclk_max_khz = cfg->cdclk_max_khz;
	config->qgv_mbps = cfg->qgv_allowed_bw;
	config->dmc_programs = cfg->dmc_fw_mask;
	config->afc_override = cfg->vbt_override_afc_startup;

	/* The mode and the link; a rate or a depth that is not positive stays 0. */
	config->mode = s->mode;
	if (s->link.rate_khz > 0)
		config->rate_khz = (uint32_t)s->link.rate_khz;
	config->lanes = (unsigned)s->link.lanes;
	if (s->link.bpp > 0)
		config->bpp = (unsigned)s->link.bpp;
	config->m_n.tu = s->link.tu;
	config->m_n.data_m = s->link.data_m;
	config->m_n.data_n = s->link.data_n;
	config->m_n.link_m = s->link.link_m;
	config->m_n.link_n = s->link.link_n;

	/* The reference clock and a combo PLL's words. */
	config->pll_ref_khz = (uint32_t)s->pll.ref_khz;
	config->pll_words.cfgcr0 = s->pll.cfgcr0;
	config->pll_words.cfgcr1 = s->pll.cfgcr1;
	config->pll_words.div0 = s->pll.div0;

	/* The framebuffer. */
	config->fb_fourcc = cfg->fb_fourcc;
	config->fb_modifier = cfg->fb_modifier;
	config->fb_width = cfg->fb_width;
	config->fb_height = cfg->fb_height;
	config->fb_pitch = cfg->fb_pitch;
	config->fb_surf = cfg->fb_surf;
}

/*
 * Sets the display version of the device.
 *
 * The tables that differ between the Gen12 platforms (buffer
 * translations, DBUF geometry, CDCLK) are chosen from it; the sequences are
 * the same text.
 */
void
drv_i915_lcd_set_display_ver(
	struct i915_display *display,
	int ver)
{
	/* The world keeps the version the platform predicates answer from. */
	display->lcd_world->i915_lcd_ver = ver;
}

/*
 * Sets the xd platform description of the device, which the modeset's
 * port PLLs and DDI clock map read.
 */
void
drv_i915_lcd_set_xd_platform(
	struct i915_display *display,
	const struct i915_xd_platform *platform)
{
	/* The world keeps the description. */
	display->lcd_world->xd_platform = platform;
}

/*
 * Returns the xd platform description of the device: the one the display
 * set, or else the first description of the display version (a host
 * test's world, which names only the version).
 */
const struct i915_xd_platform *
drv_i915_lcd_xd_platform(void)
{
	const struct i915_xd_platform *platform;
	unsigned count;
	unsigned index;
	int version;

	/* The display's own. */
	if (i915_modeset_bound_world != NULL && i915_modeset_bound_world->xd_platform != NULL)
		return i915_modeset_bound_world->xd_platform;

	/* The first description of the version. */
	version = drv_i915_lcd_display_ver();
	count = drv_i915_xd_platform_count();
	for (index = 0U; index < count; index++) {
		platform = drv_i915_xd_platform_row(index);
		if ((int)platform->display_ver == version)
			return platform;
	}

	/* Succeeded: no description of the version; the first one serves. */
	return drv_i915_xd_platform_row(0U);
}

/*
 * The register window the xd parts the modeset calls are reached through:
 * the backend hooks of the device that runs the modeset, so their writes
 * go where the rest of the modeset's go (the hardware, or a host test's
 * model).  Every user holds the modeset's locks or runs in the display's
 * start, before a modeset can.
 */
static struct i915_xd_io i915_lcd_xd_window;
static int i915_lcd_xd_window_ready;

/* Points the window at a device's backend (prepared on first use) and returns it. */
struct i915_xd_io *
drv_i915_lcd_xd_io(
	struct drm_i915_private *i915)
{
	struct i915_xd_io_hooks hooks;

	/* Prepares the window once. */
	if (i915_lcd_xd_window_ready == 0) {
		drv_i915_xd_io_init(&i915_lcd_xd_window, NULL, NULL);
		i915_lcd_xd_window_ready = 1;
	}

	/* Points it at this device's backend. */
	hooks.read = i915_lcd_xd_window_read;
	hooks.write = i915_lcd_xd_window_write;
	hooks.wait = i915_lcd_xd_window_wait;
	hooks.lock = NULL;
	hooks.context = i915;
	drv_i915_xd_io_set_hooks(&i915_lcd_xd_window, &hooks);

	/* Succeeded: the window. */
	return &i915_lcd_xd_window;
}

/* Reads a register through the device's backend; the recorder, which cannot read, answers 0. */
static uint32_t
i915_lcd_xd_window_read(
	void *context,
	uint32_t offset)
{
	struct drm_i915_private *i915;

	/* The pure word recorder has no read hook. */
	i915 = context;
	if (i915->emit->read32 == NULL)
		return 0U;

	/* Succeeded: the register's value. */
	return i915->emit->read32(i915->emit->ctx, offset);
}

/* Writes a register through the device's backend. */
static void
i915_lcd_xd_window_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	struct drm_i915_private *i915;

	/* Writes the register. */
	i915 = context;
	i915->emit->write32(i915->emit->ctx, offset, value);
}

/* Lets time pass through the device's backend, when it keeps time. */
static void
i915_lcd_xd_window_wait(
	void *context,
	unsigned microseconds)
{
	struct drm_i915_private *i915;

	/* A backend without a clock lets no time pass. */
	i915 = context;
	if (i915->emit->usleep == NULL)
		return;

	/* Sleeps. */
	i915->emit->usleep(i915->emit->ctx, microseconds);
}

/*
 * Returns the display version of the device (12 = Tiger Lake, 13 = ADL-P class).
 */
int
drv_i915_lcd_display_ver(void)
{
	/* Before a world exists the predicates answer the default version. */
	if (i915_modeset_bound_world == NULL)
		return I915_LCD_DEFAULT_DISPLAY_VER;

	/* Reports the version the probe set. */
	return i915_modeset_bound_world->i915_lcd_ver;
}

/*
 * Records an anomaly the backend found as a Linux-text error of the
 * selected screen.
 *
 * A power well kept on or a broken time base is the run's first anomaly,
 * not a silent condition.
 */
void
drv_i915_lcd_backend_fault(
	const char *what)
{
	/* Without a display there is no run to report to. */
	if (i915_modeset_bound_display == NULL)
		return;

	/* Counts the fault as an error of the screen whose operation runs. */
	i915_modeset_on_error(i915_modeset_bound_display, what);
}

/*
 * Hands a debug message of the older code to the backend of the screen whose
 * operation runs.
 */
void
drv_i915_lcd_debug(
	const char *what)
{
	const struct i915_xd_backend *backend;
	struct i915_xd_pictures *pictures;

	/* Without a display there is no backend to hand it to. */
	if (i915_modeset_bound_display == NULL)
		return;

	/* The backend of the screen whose operation runs; one without a debug hook ignores the message. */
	pictures = &i915_modeset_bound_display->xd.pictures;
	backend = pictures->screen[pictures->current].backend;
	if (backend == NULL || backend->debug == NULL)
		return;

	backend->debug(backend->ctx, what);
}

/*
 * Shows one buffer somebody else prepared and stops it safely.
 *
 * The pixels are not touched and the buffer is not created, unpinned or
 * destroyed here.  It must be pinned and unused.  On return it is either
 * pinned again (display_released: the stop was confirmed; the owner
 * releases it when its other users are done) or abandoned (the stop was
 * not confirmed: kept for ever, the device latch is set).  verify
 * (optional) re-checks the pixels after the stop.  Returns 0 when the
 * picture was up for the whole window and the stop was confirmed; EINVAL
 * or EBUSY when nothing was touched; EIO when the run did not pass.
 */
int
drv_i915_lcd_show_prepared(
	struct i915_display *display,
	struct i915_lcd_show_env *env,
	struct i915_scanout *so,
	uint32_t (*verify)(void *ctx, const struct i915_scanout *so),
	void *verify_ctx,
	struct i915_lcd_show_report *r)
{
	int begin_error;

	/* Refuses a call without its inputs. */
	if (env == NULL || r == NULL || env->hw == NULL || env->lcd == NULL || so == NULL)
		return EINVAL;

	/* An earlier run left resources the display or the GPU may still use: nothing is touched. */
	begin_error = i915_show_begin(display, env, r);
	if (begin_error != 0)
		return begin_error;

	/* The buffer must be pinned and unused. */
	if (so->state != I915_SCANOUT_PINNED) {
		i915_show_anomaly(r, "the prepared buffer is not a pinned, unused scanout buffer", EINVAL);
		return EINVAL;
	}

	i915_show_reached(env, r, I915_LCD_SHOW_BUFFER_READY);

	/* Shows it and stops it. */
	(void)i915_show_display(display, env, so, r, verify, verify_ctx);

	/* For the prepared path: the display let go; the owner releases the buffer. */
	r->released = r->display_released;
	r->pass = i915_show_passed(display, r, r->display_released);
	if (!r->pass)
		return EIO;

	/* Succeeded: the picture was up and the display let go of it. */
	return 0;
}

/*
 * Reports whether a run left resources the display or the GPU may still
 * use.
 *
 * The device-side latch is set when a run ends abandoned and never cleared
 * by a later run; every run refuses before it initialises anything while
 * it is set, and the outer teardown asks it before releasing the DMA
 * device, the scratch page, the BARs or bus mastering.
 */
int
drv_i915_lcd_show_retained(
	struct i915_display *display)
{
	/* The display was not shown to have stopped. */
	if (display->show_retained)
		return 1;

	/* The GPU was not shown to be done. */
	if (display->show_gpu_retained)
		return 1;

	/* Nothing is retained. */
	return 0;
}

/*
 * Reports whether the GPU was not shown to be done with a retained buffer.
 */
int
drv_i915_lcd_show_gpu_retained(
	struct i915_display *display)
{
	/* Reports the latch. */
	return display->show_gpu_retained;
}

/*
 * Lights the panel as the display of the resident node.
 *
 * Two full-panel buffers, one modeset, synchronous flips: buffer A is
 * shown through the show body, and serve runs inside its observation
 * window (after the first frames were seen) until the display is to be
 * given back; the reference's stop path follows and both buffers are
 * released.  When display=hdmi chose the HDMI display (output.c), it is
 * lit in the panel's place: port B, pipe B, DVI mode, the chosen mode,
 * and the panel is not touched.  Returns 0 when the output came up and was
 * stopped and released cleanly; EINVAL (a dependency is missing), EBUSY
 * (resources of an earlier run are retained), EAGAIN (an external DP link
 * did not train and the port's link was lowered: the caller runs again) or
 * EIO otherwise.
 */
int
drv_i915_lcd_kernel_resident_run(
	struct i915_display *display,
	const struct i915_lcd_kernel_deps *d,
	int (*serve)(void *ctx),
	void *ctx)
{
	struct i915_lcd_run_params output_params;
	struct i915_resident_output_way way;
	const struct i915_lcd_state *lcd;
	struct i915_lcd_kernel *k;
	struct i915_lcd_show_env *env;
	struct i915_lcd_show_report *rep;
	const char *verdict;
	const char *stop;
	const char *first_anomaly;
	int show_result;
	int released;
	int passed;
	int held;
	int retained;
	int buffers_error;
	int preflight_error;
	int fill_error;
	int takeover_error;
	int way_error;
	int fallback_error;
	struct i915_xd_run_outcome outcome;
	struct i915_xd_run_result run;
	int run_error;
	int debug;
	int unplugged;
	int kept;
	unsigned domain;

	k = &display->lk;
	env = &display->resident_run_env;
	rep = &display->resident_run_rep;

	/* The run needs the panel, the registers, the GT memory, the interrupts and a serve loop. */
	if (d == NULL || d->edp == NULL || d->mmio == NULL || d->gm == NULL || d->irq == NULL || serve == NULL) {
		kern_logf("i915: resident display: not started (a dependency is missing)\n");
		return EINVAL;
	}

	/*
	 * An earlier run left resources the display may still read: refused.
	 * Buffers an earlier run kept pinned for this one, when the resident
	 * output is lit again for two pipes, are this run's.
	 */
	retained = drv_i915_lcd_show_retained(display);
	if (!retained)
		retained = drv_i915_xd_picture_retained(&display->xd.pictures, I915_XD_PICTURE_RESIDENT);
	kept = i915_resident_buffers_kept(display);
	if (retained) {
		kern_logf("i915: resident display: refused (resources of an earlier run are retained)\n");
		return EBUSY;
	}

	/* Buffers of an earlier run that were not kept for this one are retained. */
	if (!kept &&
	    (display->resident_buf[0].state != I915_SCANOUT_NONE ||
	     display->resident_buf[1].state != I915_SCANOUT_NONE)) {
		kern_logf("i915: resident display: refused (resources of an earlier run are retained)\n");
		return EBUSY;
	}

	/* The two modeset locks live as long as the device. */
	drv_i915_lcd_kernel_locks_init(display);

	/* Starts the run with its hooks bound over the normal initialisation's objects. */
	kern_memset(k, 0, sizeof(*k));
	k->locks = display->lcdb_locks;
	k->d = d;
	drv_i915_lcd_kernel_bind_ops(k);

	/*
	 * The display a UEFI boot's firmware left lit (the firmware check found
	 * a lit pipe): read out and stopped before this run writes.
	 */
	if (display->xd.firmware.take_over) {
		takeover_error = i915_resident_takeover(display, k);
		if (takeover_error != 0) {
			k->p = NULL;
			return EIO;
		}

		/* Taken over once: a later run starts from the driver's own display. */
		display->xd.firmware.take_over = 0;

		/* The INIT reference the probe kept for the takeover goes back now (intel_power_domains_enable()). */
		if (display->dprobe.power_domains_enable_deferred) {
			display->dprobe.power_domains_enable_deferred = 0;
			drv_i915_power_domains_enable(&display->dprobe, &display->dcore);
			kern_logf("i915: takeover: power_domains_enable: wells_on %u -> %u\n", display->dprobe.wells_on_before, display->dprobe.wells_on_after);
		}
	}

	/* How the output's kind is lit (the panel: the run's defaults). */
	lcd = &d->edp->lcd;
	way_error = i915_resident_output_way(display->output.kind, &way);
	if (way_error != 0) {
		kern_logf("i915: resident display: not started: this driver does not light the output's kind (%d) yet\n", (int)display->output.kind);
		return EIO;
	}

	/*
	 * Another output in the panel's place: the
	 * run's parameters name its port and pipe, and, as the only screen, it
	 * may own the device's PLL pool and DBUF state from their empty start.
	 */
	if (way.params != NULL) {
		way.params(&display->output, &output_params);
		k->p = &output_params;
		lcd = &display->output.state;
		if (way.own_pool)
			drv_i915_xd_pictures_forget(&display->xd.pictures);
	}

	/* The hardware must be as the initialisation left it, and the inputs must be complete. */
	kern_memset(env, 0, sizeof(*env));
	preflight_error = drv_i915_lcd_kernel_preflight(k);
	fill_error = 0;
	if (preflight_error == 0)
		fill_error = drv_i915_lcd_kernel_fill_cfg(k, &env->cfg);
	if (preflight_error != 0 || fill_error != 0) {
		kern_logf("i915: resident display: not started (preflight: nothing was written)\n");
		k->p = NULL;
		return EIO;
	}

	/* The output's encoder in place of the panel's eDP. */
	if (way.cfg != NULL)
		way.cfg(display, d, &display->output, &env->cfg);

	/*
	 * The DBUF share leaves room for a claimed second output's pipe;
	 * without one the output has the DBUF to itself, as before.  The
	 * window remembers which pipes the run was lit for.
	 */
	display->window.run_pipes = drv_i915_head_run_pipes(display);
	env->cfg.also_active_pipes = display->window.run_pipes;

	/* Kept buffers of another size (the output changed meanwhile) are given back and made again. */
	if (kept &&
	    (display->resident_buf[0].width != (uint32_t)lcd->mode.hdisplay ||
	     display->resident_buf[0].height != (uint32_t)lcd->mode.vdisplay)) {
		(void)drv_i915_scanout_unpin(&display->resident_buf[1]);
		(void)drv_i915_scanout_destroy(&display->resident_buf[1]);
		(void)drv_i915_scanout_unpin(&display->resident_buf[0]);
		(void)drv_i915_scanout_destroy(&display->resident_buf[0]);
		display->window.keep_buffers = 0;
		kept = 0;
	}

	/* Creates, pins, clears and publishes both buffers at the output's size, or takes the ones kept for this run. */
	if (kept) {
		display->window.keep_buffers = 0;
		kern_logf("i915: resident display: the buffers and their picture are kept for this lighting (pipes besides it 0x%x)\n", display->window.run_pipes);
	} else {
		buffers_error = i915_resident_buffers(display, d, lcd);
		if (buffers_error != 0) {
			k->p = NULL;
			return EIO;
		}
	}

	/* Buffer A is shown first; the serve loop runs in the window. */
	display->resident_front = 0U;
	display->resident_serve = serve;
	display->resident_serve_ctx = ctx;
	k->flip_a = &display->resident_buf[0];
	k->flip_b = &display->resident_buf[1];

	/* The show environment: the window is the time serve runs. */
	env->hw = &k->ops;
	env->gm = d->gm;
	env->lcd = lcd;
	env->pipe = 0;
	env->first_frames_ms = I915_LCD_FIRST_FRAMES_MS;
	env->window_ms = 0U;
	env->in_window = i915_resident_window;
	env->in_window_ctx = k;
	env->at_stage = drv_i915_lcd_kernel_at_stage;
	env->at_stage_ctx = k;
	k->window_ms = 0U;

	/* The output's pipe (the panel's is pipe A). */
	env->pipe = (int)drv_i915_display_output_pipe(&display->output);

	/* Shows buffer A, serves in the window, and stops through the reference's path. */
	show_result = drv_i915_lcd_show_prepared(display, env, &display->resident_buf[0], i915_resident_verify, NULL, rep);

	/* Counts the power references the run still holds. */
	held = 0;
	for (domain = 0U; domain < I915_PW_DOMAIN_NUM; domain++)
		held += k->power_refs[domain];

	/* Releases both buffers, or abandons them when the display may still read them. */
	released = i915_resident_release(display, rep);

	/*
	 * Judges the run on the counters: the run log holds a bounded number of
	 * entries and a display that flips for as long as an application
	 * presents always outgrows it.  i915.debug=display prints the run log
	 * and the observer whatever the counters say.
	 */
	passed = i915_resident_passed(rep);
	debug = drv_i915_lcd_debug_enabled();
	unplugged = __atomic_load_n(&display->window.unplugged, __ATOMIC_ACQUIRE);
	if (debug || ((!passed || !released || held != 0) && !unplugged)) {
		drv_i915_lcd_log_trace(rep->trace);
		drv_i915_lcd_log_observer(&rep->obs);
	} else if (unplugged) {
		/* A run whose display was unplugged fails as expected: its log would only delay the output's return. */
		kern_logf("i915: resident display: run log not printed: its display was unplugged (%u entries kept)\n", rep->trace->n);
	} else {
		kern_logf("i915: resident display: run log %u writes, %u rmw, %u waits (0 timed out), 0 errors, 0 unresolved steps (%u entries kept, %u not kept)\n",
		    rep->trace->writes,
		    rep->trace->rmws,
		    rep->trace->waits,
		    rep->trace->n,
		    rep->trace->dropped);
	}

	/* Reports how the run ended. */
	verdict = "FAIL";
	if (passed && released && held == 0)
		verdict = "PASS";

	stop = "NOT confirmed";
	if (rep->display_released)
		stop = "confirmed";

	first_anomaly = "none";
	if (rep->first_anomaly != NULL)
		first_anomaly = rep->first_anomaly;

	kern_logf("i915: resident display: ended %s (show rc=%d; flips %u, stop %s, buffers released=%d, power refs held %d, first anomaly: %s)\n",
	    verdict,
	    show_result,
	    k->flips_done,
	    stop,
	    released,
	    held,
	    first_anomaly);

	/* The serve loop is no longer called, and the run's parameters are gone. */
	display->resident_serve = NULL;
	k->p = NULL;

	/*
	 * An external DP link that did not train, stopped and given back
	 * cleanly: the port's link limits are lowered and the run is to be
	 * tried again at the lower link.
	 */
	kern_memset(&outcome, 0, sizeof(outcome));
	if (!passed && released && held == 0 && display->output.kind == I915_OUTPUT_KIND_DP_EXT) {
		fallback_error = i915_resident_dp_ext_fallback(display, &display->output, rep->enable_rc);
		if (fallback_error == 0)
			outcome.lowered = 1;
	}

	/* The screen's verdict on the run. */
	outcome.passed = passed;
	outcome.released = released;
	outcome.held = held;
	outcome.unplugged = unplugged;
	outcome.acquired = rep->display_acquired;
	run_error = drv_i915_xd_screen_run_verdict(&outcome, &run);
	if (run_error != 0)
		return run_error;

	/* Succeeded: the panel came up, stopped and was released. */
	return 0;
}

/*
 * Fills the run parameters of an output that is not the built-in panel,
 * for the second output's run: the kind's port, pipe,
 * transcoder and PLL, as the resident run of that kind has them.  The
 * second output never empties the PLL pool the resident output draws from.
 * Returns 0, or EOPNOTSUPP for the panel (which has no parameters of its
 * own) or a kind this driver does not light.
 */
int
drv_i915_lcd_output_params(
	const struct i915_display_output *output,
	struct i915_lcd_run_params *params)
{
	struct i915_resident_output_way way;
	int way_error;

	/* The kind's way; the panel's has no parameters. */
	way_error = i915_resident_output_way(output->kind, &way);
	if (way_error != 0)
		return EOPNOTSUPP;
	if (way.params == NULL)
		return EOPNOTSUPP;

	/* The kind's parameters, beside the resident output's PLL. */
	way.params(output, params);
	params->reset_dplls = 0;

	/* Succeeded: the parameters name the output. */
	return 0;
}

/*
 * Turns a filled panel configuration into the encoder's of an output that
 * is not the built-in panel, for the second output's run.
 * The panel's configuration is left as it is.
 */
void
drv_i915_lcd_output_cfg(
	struct i915_display *display,
	const struct i915_lcd_kernel_deps *d,
	const struct i915_display_output *output,
	struct i915_lcd_modeset_cfg *cfg)
{
	struct i915_resident_output_way way;
	int way_error;

	/* The kind's way; the panel's leaves the configuration as it is. */
	way_error = i915_resident_output_way(output->kind, &way);
	if (way_error != 0)
		return;
	if (way.cfg == NULL)
		return;

	/* The kind's encoder. */
	way.cfg(display, d, output, cfg);
}

/*
 * Lowers an external DP output's link after an enable that reported it as
 * not trained, for the second output's run, as the resident
 * run does for its own output.  Returns 0 when the output may be lit again
 * at the lower link, ENOSPC when no lower link carries the mode, or EINVAL
 * for an output or an enable that did not fail at the link.
 */
int
drv_i915_lcd_output_link_fallback(
	struct i915_display *display,
	struct i915_display_output *output,
	int enable_rc)
{
	int error;

	/* Only an external DP output trains a link. */
	if (output->kind != I915_OUTPUT_KIND_DP_EXT)
		return EINVAL;

	/* The resident run's fallback, on this output. */
	error = i915_resident_dp_ext_fallback(display, output, enable_rc);
	if (error != 0)
		return error;

	/* Succeeded: the output may be lit again. */
	return 0;
}

/*
 * Creates the two modeset mutexes once.
 *
 * They live as long as the device.
 */
void
drv_i915_lcd_kernel_locks_init(
	struct i915_display *display)
{
	/* The mutexes exist already. */
	if (display->lcdb_locks_live)
		return;

	/* The dpll lock and the backlight lock. */
	(void)mutex_init(&display->lcdb_locks[I915_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "i915-lcd-dpll");
	(void)mutex_init(&display->lcdb_locks[I915_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "i915-lcd-backlight");
	display->lcdb_locks_live = 1;
}

/*
 * Binds the hooks of a panel run: registers, waits and time here; DPCD and
 * panel power through the resident eDP; power domains, vblank and events,
 * and the note, step, error and debug hooks through their owners.  The
 * observe hook is left to the run log's tap; the observer reads through
 * these hooks directly.
 */
void
drv_i915_lcd_kernel_bind_ops(
	struct i915_lcd_kernel *k)
{
	/* Starts with every hook empty; the run is their context. */
	kern_memset(&k->ops, 0, sizeof(k->ops));
	k->ops.ctx = k;

	/* Registers, waits and time. */
	k->ops.write32 = i915_kernel_write32;
	k->ops.rmw32 = i915_kernel_rmw32;
	k->ops.posting_read = i915_kernel_posting_read;
	k->ops.read32 = i915_kernel_read32;
	k->ops.wait_reg = i915_kernel_wait_reg;
	k->ops.usleep = i915_kernel_usleep;
	k->ops.udelay = i915_kernel_udelay;

	/* The sink's DPCD and the panel power sequencer of the resident eDP. */
	k->ops.dpcd_read = drv_i915_edp_emit_dpcd_read;
	k->ops.dpcd_write = drv_i915_edp_emit_dpcd_write;
	k->ops.read_dpcd_caps = drv_i915_edp_emit_read_dpcd_caps;
	k->ops.panel = drv_i915_edp_emit_panel;

	/* Display power and the DBUF slices. */
	k->ops.power_domain = drv_i915_lcd_power_domain_of;
	k->ops.power_get = drv_i915_lcd_power_get;
	k->ops.power_get_if_enabled = drv_i915_lcd_power_get_if_enabled;
	k->ops.power_put = drv_i915_lcd_power_put;
	k->ops.power_put_async = drv_i915_lcd_power_put_async;
	k->ops.dbuf_slices_update = i915_kernel_dbuf_slices_update;

	/* The locks, the steps, the errors and the debug messages. */
	k->ops.lock = i915_kernel_lock;
	k->ops.step = drv_i915_lcd_kernel_step;
	k->ops.error = drv_i915_lcd_kernel_error;
	k->ops.debug = drv_i915_lcd_kernel_debug;

	/* The Type-C ports' links, which an output's disable gives back. */
	k->ops.tc_put_link = drv_i915_lcd_tc_put_link;
	k->ops.tc_get_link = drv_i915_lcd_tc_get_link;
	k->ops.tc_mode = drv_i915_lcd_tc_mode;
	k->ops.tc_set_fia_lane_count = drv_i915_lcd_tc_set_fia_lane_count;
	k->ops.tc_pin_assignment = drv_i915_lcd_tc_pin_assignment;

	/* The Type-C ports' DKL PHYs, behind the bank index their lock orders. */
	k->ops.dkl_read = drv_i915_lcd_dkl_read;
	k->ops.dkl_write = drv_i915_lcd_dkl_write;
	k->ops.dkl_rmw = drv_i915_lcd_dkl_rmw;

	/* The synchronous update: vblank references, the vblank sleep, the section's interrupts and the event. */
	k->ops.vblank_get = drv_i915_lcd_kernel_vblank_get;
	k->ops.vblank_put = drv_i915_lcd_kernel_vblank_put;
	k->ops.vblank_sleep = drv_i915_lcd_kernel_vblank_sleep;
	k->ops.irq_off = i915_kernel_irq_off;
	k->ops.irq_on = i915_kernel_irq_on;
	k->ops.arm_event = drv_i915_lcd_kernel_arm_event;
	k->ops.wait_event = drv_i915_lcd_kernel_wait_event;
	k->ops.cancel_event = drv_i915_lcd_kernel_cancel_event;
}

/*
 * Checks that the hardware is in the state the normal initialisation
 * leaves; anything else is not this run's to fix.  Returns 0, or EBUSY
 * when nothing may be written.
 */
int
drv_i915_lcd_kernel_preflight(
	struct i915_lcd_kernel *k)
{
	struct i915_display *display;
	const struct i915_lcd_kernel_deps *d;
	uint32_t transconf;
	uint32_t plane_ctl;
	uint32_t pll;
	uint32_t ddi;
	uint32_t imr;
	uint32_t want;
	int idle;
	int masked;
	int ok;
	int hdmi_error;
	int dp_ext_error;

	display = i915_kernel_display(k);
	d = k->d;
	ok = 1;

	/* An HDMI run checks its own pipe and port. */
	if (k->p != NULL && k->p->output_hdmi) {
		hdmi_error = i915_kernel_preflight_hdmi(k);
		if (hdmi_error != 0)
			return hdmi_error;

		return 0;
	}

	/* So does an external DP run. */
	if (k->p != NULL && k->p->output_dp_ext) {
		dp_ext_error = i915_kernel_preflight_dp_ext(k);
		if (dp_ext_error != 0)
			return dp_ext_error;

		/* Succeeded: the external DP output may be written. */
		return 0;
	}

	/* The resident eDP must be live and its state computed. */
	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {
		kern_logf("i915: LCD-B preflight: the resident eDP is not live (connector_live=%d lcd_rc=%d)\n",
		    d->edp->connector_live,
		    d->edp->lcd_rc);
		return EBUSY;
	}

	/* The scanout buffer needs the GT memory's GGTT. */
	if (!d->gm->inited) {
		kern_logf("i915: LCD-B preflight: no GT memory (GGTT) for the scanout buffer\n");
		return EBUSY;
	}

	/* Logs the registers and reads the pipe, plane, PLL, DDI and interrupt mask. */
	drv_i915_lcd_log_regs(display->lcd_world, k, "preflight", 0);
	transconf = drv_i915_read32(d->mmio, drv_i915_lcd_reg_by_name(display->lcd_world, "TRANSCONF"));
	plane_ctl = drv_i915_read32(d->mmio, drv_i915_lcd_reg_by_name(display->lcd_world, "PLANE_CTL_1"));
	pll = drv_i915_read32(d->mmio, drv_i915_lcd_reg_by_name(display->lcd_world, "ICL_DPLL_ENABLE"));
	ddi = drv_i915_read32(d->mmio, drv_i915_lcd_reg_by_name(display->lcd_world, "DDI_BUF_CTL"));
	imr = drv_i915_read32(d->mmio, drv_i915_lcd_reg_by_name(display->lcd_world, "GEN8_DE_PIPE_IMR"));

	/* A running display means the start conditions are not the prepared ones: nothing may be written. */
	if ((transconf & I915_LCD_TRANSCONF_ON_MASK) != 0U ||
	    (plane_ctl & I915_LCD_ENABLE_BIT) != 0U ||
	    (pll & I915_LCD_ENABLE_BIT) != 0U ||
	    (ddi & I915_LCD_ENABLE_BIT) != 0U) {
		kern_logf("i915: LCD-B preflight: the display is not idle (TRANSCONF 0x%08x PLANE_CTL 0x%08x DPLL 0x%08x DDI_BUF_CTL 0x%08x)\n",
		    transconf,
		    plane_ctl,
		    pll,
		    ddi);
		ok = 0;
	}

	/*
	 * Pipe A's registers sit in power well A, which is off until the
	 * commit takes the pipe's power domain: they read 0 here and say
	 * nothing.  Checked instead: the interrupt state's mask, which the
	 * power-well enable programs, keeps vblank (bit 0) and underrun (bit
	 * 31, and bits 22 / 21 from display version 13) masked.
	 */
	want = I915_LCD_KEEP_MASKED_GEN12;
	if (drv_i915_lcd_display_ver() >= 13)
		want = I915_LCD_KEEP_MASKED_XELPD;

	if ((d->irq->de_irq_mask[0] & want) != want) {
		kern_logf("i915: LCD-B preflight: the IRQ state's pipe A mask 0x%08x would leave vblank / underrun unmasked (this display needs 0x%08x)\n",
		    d->irq->de_irq_mask[0],
		    want);
		ok = 0;
	}

	/* Logs what was found. */
	idle = 0;
	if ((pll & I915_LCD_ENABLE_BIT) == 0U && (ddi & I915_LCD_ENABLE_BIT) == 0U)
		idle = 1;

	masked = 0;
	if ((d->irq->de_irq_mask[0] & I915_LCD_KEEP_MASKED_XELPD) == I915_LCD_KEEP_MASKED_XELPD)
		masked = 1;

	kern_logf("i915: LCD-B preflight: DDI / PLL idle=%d (pipe A registers read TRANSCONF 0x%08x PLANE_CTL 0x%08x IMR 0x%08x with power well A off) | IRQ state pipe A mask 0x%08x: vblank + underrun masked=%d | no code of this path waits for a software vblank count; no worker / callback / waiter is created by it\n",
	    idle,
	    transconf,
	    plane_ctl,
	    imr,
	    d->irq->de_irq_mask[0],
	    masked);

	/* The display is not in the prepared state. */
	if (!ok)
		return EBUSY;

	/* Succeeded: the run may write the display. */
	return 0;
}

/*
 * Fills the modeset configuration of a panel run from the objects the
 * normal initialisation built: the VBT panel data, the resident eDP's
 * capabilities and readouts, the watermark latencies, the DBUF, CDCLK and
 * QGV state, and the loaded DMC firmware.  Returns 0, or EINVAL when the
 * VBT has no panel data for port A.
 */
int
drv_i915_lcd_kernel_fill_cfg(
	struct i915_lcd_kernel *k,
	struct i915_lcd_modeset_cfg *c)
{
	struct i915_display *display;
	const struct i915_lcd_kernel_deps *d;
	struct i915_edp_device *edp;
	struct i915_vbt_panel *pn;
	unsigned allowed;
	unsigned point;
	unsigned i;
	uint32_t mbus;
	int panel_error;

	display = i915_kernel_display(k);
	d = k->d;
	edp = d->edp;
	pn = &display->fill_cfg_pn;

	/* Reads the panel's VBT data for port A. */
	kern_memset(c, 0, sizeof(*c));
	panel_error = drv_i915_vbt_init_panel(&edp->vbt->parsed, 0, edp->res.edid, pn);
	if (panel_error != 0) {
		kern_logf("i915: LCD-B input: the VBT has no panel data for port A (rc=%d)\n", panel_error);
		return EINVAL;
	}

	/* Port A, pipe and transcoder A, DPLL 0 expected, AUX A. */
	c->port = 0;
	c->pipe = 0;
	c->cpu_transcoder = 0;
	c->dpll_id = 0;
	c->aux_ch = 0;

	/* intel_ddi_init(): the port reversal and four-lane bits of the readout at connector init. */
	c->saved_port_bits = edp->ddi_buf_ctl_readout & I915_LCD_SAVED_PORT_BITS;

	/* The sink's capabilities from the resident eDP. */
	kern_memcpy(c->dpcd, edp->res.dpcd, sizeof(c->dpcd));
	kern_memcpy(c->edp_dpcd, edp->res.edp_dpcd, sizeof(c->edp_dpcd));

	/* The panel's VBT eDP and backlight blocks. */
	c->vbt_low_vswing = pn->edp_low_vswing;
	c->vbt_hobl = pn->edp_hobl;
	c->vbt_override_afc_startup = pn->override_afc_startup;
	c->vbt_backlight_present = pn->bl_present;
	c->vbt_backlight_active_low = pn->bl_active_low_pwm;
	c->vbt_backlight_controller = pn->bl_controller;
	c->vbt_backlight_pwm_freq_hz = pn->bl_pwm_freq_hz;
	c->vbt_backlight_min_brightness = pn->bl_min_brightness;
	k->vbt_min = pn->bl_min_brightness;
	c->rawclk_khz = edp->cfg.rawclk_khz;

	/* The watermark latencies and the SAGV block time the PCODE readout left. */
	for (i = 0U; i < 8U && i < I915_NOGEM_MAX_WM_LVL; i++)
		c->wm_latency[i] = d->nogem->wm_skl_latency[i];

	c->wm_num_levels = (uint8_t)d->nogem->wm_num_levels;
	c->wm_ipc_enabled = d->ipc_enabled;
	c->sagv_block_time_us = (uint8_t)d->nogem->sagv_block_time_us;

	/*
	 * The DBUF geometry of this display (intel_display_device.c): XE_LPD
	 * has 4096 blocks over four slices, Tiger Lake 2048 over two.
	 */
	c->dbuf_size = I915_LCD_DBUF_XELPD_BLOCKS;
	if (d->dcore->dbuf_slice_mask == I915_LCD_DBUF_TGL_SLICES)
		c->dbuf_size = I915_LCD_DBUF_TGL_BLOCKS;

	c->dbuf_slice_mask = I915_LCD_DBUF_XELPD_SLICES;
	if (d->dcore->dbuf_slice_mask != 0U)
		c->dbuf_slice_mask = d->dcore->dbuf_slice_mask;

	/* The DBUF slices enabled now, and whether MBUS is joined. */
	c->dbuf_enabled_slices = d->dcore->dbuf_enabled_slices;
	mbus = drv_i915_read32(d->mmio, drv_i915_lcd_reg_by_name(display->lcd_world, "MBUS_CTL"));
	c->mbus_joined = (mbus >> 31) & 1U;

	/* The CDCLK state the init left, and intel_update_max_cdclk() for display version 11+. */
	c->cdclk_khz = d->cdclk->hw.cdclk;
	c->cdclk_vco_khz = d->cdclk->hw.vco;
	c->cdclk_ref_khz = d->cdclk->hw.ref;
	c->cdclk_bypass_khz = d->cdclk->hw.bypass;
	c->cdclk_voltage_level = d->cdclk->hw.voltage_level;
	c->cdclk_max_khz = drv_i915_max_cdclk_freq(d->cdclk);

	/* SAGV: the forced disable left exactly one QGV point unmasked; its derated bandwidth for one active plane. */
	allowed = (unsigned)~d->dstate->bw_obj_state.qgv_points_mask & ((1U << d->bw->max[0].num_qgv_points) - 1U);
	if (allowed != 0U && (allowed & (allowed - 1U)) == 0U) {
		/* Finds the one allowed point. */
		point = 0U;
		while ((allowed & (1U << point)) == 0U)
			point++;

		c->qgv_allowed_bw = drv_i915_icl_qgv_bw(d->bw, 13, 1, (int)point);
	}

	/* The DMC firmware ids whose payload is loaded. */
	for (i = 0U; i < I915_DMC_FW_MAX; i++) {
		if (d->dmc->dmc.dmc_info[i].payload != NULL && d->dmc->dmc.dmc_info[i].present)
			c->dmc_fw_mask |= 1U << i;
	}

	/* Logs every input and where it came from. */
	kern_logf("i915: LCD-B input: VBT low_vswing=%d hobl=%d afc_override=%d backlight present=%d active_low=%d controller=%d pwm=%u Hz min=%u | rawclk=%u kHz | DDI_BUF_CTL at connector init 0x%08x -> saved bits 0x%x\n",
	    c->vbt_low_vswing,
	    c->vbt_hobl,
	    c->vbt_override_afc_startup,
	    c->vbt_backlight_present,
	    c->vbt_backlight_active_low,
	    c->vbt_backlight_controller,
	    c->vbt_backlight_pwm_freq_hz,
	    c->vbt_backlight_min_brightness,
	    c->rawclk_khz,
	    edp->ddi_buf_ctl_readout,
	    c->saved_port_bits);
	kern_logf("i915: LCD-B input: wm levels=%u latency %u/%u/%u/%u/%u/%u/%u/%u us ipc=%d sagv_block=%u us | DBUF slices now 0x%x MBUS_CTL=0x%08x joined=%d | CDCLK now %u kHz vco %u ref %u bypass %u level %u max %u | QGV mask 0x%x -> allowed 0x%x bw %u MB/s | DMC fw mask 0x%x\n",
	    c->wm_num_levels,
	    c->wm_latency[0],
	    c->wm_latency[1],
	    c->wm_latency[2],
	    c->wm_latency[3],
	    c->wm_latency[4],
	    c->wm_latency[5],
	    c->wm_latency[6],
	    c->wm_latency[7],
	    c->wm_ipc_enabled,
	    c->sagv_block_time_us,
	    c->dbuf_enabled_slices,
	    mbus,
	    c->mbus_joined,
	    c->cdclk_khz,
	    c->cdclk_vco_khz,
	    c->cdclk_ref_khz,
	    c->cdclk_bypass_khz,
	    c->cdclk_voltage_level,
	    c->cdclk_max_khz,
	    (unsigned)d->dstate->bw_obj_state.qgv_points_mask,
	    allowed,
	    c->qgv_allowed_bw,
	    c->dmc_fw_mask);

	/* Succeeded: the configuration is complete. */
	return 0;
}

/*
 * Reacts to a stage of a panel run: the cleanup phase, the registers at
 * picture-up and after the disable.
 */
void
drv_i915_lcd_kernel_at_stage(
	void *ctx,
	int stage)
{
	struct i915_lcd_kernel *k;
	struct i915_display *display;
	int debug;

	k = ctx;
	display = i915_kernel_display(ctx);

	/* Reacts to the stages the run log names. */
	switch (stage) {
	case I915_LCD_SHOW_ENABLE_RETURNED:
		/* Whatever the reference reports from here on belongs to the way down: counted apart. */
		k->phase_cleanup = 1;

		/* i915.debug=display prints the enable commit's run log now, failed or not. */
		debug = drv_i915_lcd_debug_enabled();
		if (debug) {
			kern_logf("i915: LCD-B debug: the run log up to the enable commit's return follows\n");
			drv_i915_lcd_log_trace(&display->show_trace);
		}

		break;
	case I915_LCD_SHOW_PICTURE_UP:
		/* The registers next to Linux's dump, and the start of the window. */
		drv_i915_lcd_log_regs(display->lcd_world, k, "picture-up", 1);
		kern_logf("i915: LCD-B PICTURE UP: pattern id=%u on the panel; observation window %u ms starts now (take the photograph)\n",
		    k->pattern_id,
		    k->window_ms);
		break;
	case I915_LCD_SHOW_WINDOW_DONE:
		kern_logf("i915: LCD-B observation window over; stopping through the reference's disable path\n");
		break;
	case I915_LCD_SHOW_DISABLE_RETURNED:
		drv_i915_lcd_log_regs(display->lcd_world, k, "after-disable", 0);
		break;
	default:
		break;
	}
}

/* Counts an error of the older code against the screen whose operation runs (ctx: the display). */
static void
i915_modeset_on_error(
	void *ctx,
	const char *what)
{
	struct i915_display *display;

	/* The display's pictures count it. */
	display = ctx;
	drv_i915_xd_pictures_fault(&display->xd.pictures, what);
}

/* Records the first anomaly of a run; later ones are its consequences. */
static void
i915_show_anomaly(
	struct i915_lcd_show_report *r,
	const char *what,
	int rc)
{
	/* The first one is the finding. */
	if (r->first_anomaly != NULL)
		return;

	r->first_anomaly = what;
	r->first_anomaly_stage = r->stage;
	r->first_anomaly_rc = rc;
}

/* Records a stage the run reached and tells the caller's stage hook. */
static void
i915_show_reached(
	struct i915_lcd_show_env *env,
	struct i915_lcd_show_report *r,
	int stage)
{
	/* The furthest stage so far. */
	r->stage = stage;

	/* The caller logs registers or injects faults here. */
	if (env->at_stage != NULL)
		env->at_stage(env->at_stage_ctx, stage);
}

/* Finds the first error entry of the run log at or after an index; -1 when none. */
static int
i915_show_first_error_after(
	const struct i915_lcd_trace *trace,
	unsigned from)
{
	unsigned index;

	/* Scans the entries from the index on. */
	for (index = from; index < trace->n; index++) {
		if (trace->e[index].kind == I915_LCD_T_ERROR)
			return (int)index;
	}

	/* No error was recorded there. */
	return -1;
}

/*
 * The display part of a run: the check phase, the enable commit, the
 * observation window, the disable commit, and the decision whether the
 * display has let go of the buffer.  Never creates, fills, unpins or
 * destroys the buffer.  Returns 1 when the buffer was handed to the display
 * (the enable was attempted), 0 when it never was.
 */
static int
i915_show_display(
	struct i915_display *display,
	struct i915_lcd_show_env *env,
	struct i915_scanout *so,
	struct i915_lcd_show_report *r,
	uint32_t (*verify)(void *ctx, const struct i915_scanout *so),
	void *verify_ctx)
{
	struct i915_xd_picture_config config;
	struct i915_xd_pictures *pictures;
	struct i915_lcd_trace *trace;
	unsigned disable_from;
	unsigned waited;
	uint32_t frame0;

	trace = &display->show_trace;
	pictures = &display->xd.pictures;
	r->surf = so->surf;

	/* The check phase: the framebuffer is the one the buffer describes. */
	env->cfg.fb_fourcc = so->format;
	env->cfg.fb_modifier = so->modifier;
	env->cfg.fb_width = so->width;
	env->cfg.fb_height = so->height;
	env->cfg.fb_pitch = so->pitch;
	env->cfg.fb_surf = (uint32_t)so->surf;

	/* The run log stacks on the backend, and the observer samples at every commit point. */
	drv_i915_lcd_trace_init(trace, env->hw);
	drv_i915_lcd_observer_init(&r->obs, env->hw, env->pipe);
	trace->tap = drv_i915_lcd_observer_point;
	trace->tap_ctx = &r->obs;

	/* Builds and checks the state. */
	drv_i915_lcd_picture_config(env->lcd, &env->cfg, &config);
	r->prepare_rc = drv_i915_xd_picture_prepare(pictures, I915_XD_PICTURE_RESIDENT, &config, &trace->ops);
	drv_i915_xd_picture_status(pictures, I915_XD_PICTURE_RESIDENT, &r->at_enable);
	if (r->prepare_rc != 0) {
		if (r->at_enable.first_error != NULL) {
			i915_show_anomaly(r, r->at_enable.first_error, r->prepare_rc);
		} else {
			i915_show_anomaly(r, "modeset prepare refused the state", r->prepare_rc);
		}

		return 0;
	}

	i915_show_reached(env, r, I915_LCD_SHOW_PREPARED);

	/* Commit 1: from here on the display may read the buffer. */
	r->begin_rc = drv_i915_scanout_begin(so);
	if (r->begin_rc != 0) {
		i915_show_anomaly(r, "scanout begin", r->begin_rc);
		return 0;
	}

	r->display_acquired = 1;
	drv_i915_lcd_trace_phase(trace, "commit: enable");
	r->enable_rc = drv_i915_xd_picture_light(pictures, I915_XD_PICTURE_RESIDENT);
	drv_i915_xd_picture_status(pictures, I915_XD_PICTURE_RESIDENT, &r->at_enable);
	drv_i915_xd_picture_link_status(pictures, I915_XD_PICTURE_RESIDENT, &r->at_enable);
	r->enable_errors = r->at_enable.errors;
	r->first_error_trace_at = trace->first_error_at;
	if (r->enable_rc != I915_LCD_MS_OK) {
		if (r->at_enable.first_error != NULL) {
			i915_show_anomaly(r, r->at_enable.first_error, r->enable_rc);
		} else {
			i915_show_anomaly(r, "the enable commit did not succeed", r->enable_rc);
		}
	}

	i915_show_reached(env, r, I915_LCD_SHOW_ENABLE_RETURNED);

	/* "The arm was written" is a software fact; whether the timing runs is asked of the frame counter. */
	if (r->enable_rc == I915_LCD_MS_OK) {
		r->first_frames_rc = drv_i915_lcd_observer_frames(&r->obs, env->first_frames_ms, I915_LCD_FIRST_FRAMES, &r->frame_first, &r->frame_last);
		if (r->first_frames_rc != 0)
			i915_show_anomaly(r, "the pipe's frame counter does not advance after the enable", r->first_frames_rc);
	}

	/* The picture is up: the in-window test, then the finite window. */
	if (r->enable_rc == I915_LCD_MS_OK && r->first_frames_rc == 0) {
		drv_i915_lcd_observer_steady_begin(&r->obs);
		i915_show_reached(env, r, I915_LCD_SHOW_PICTURE_UP);

		/* A test that runs while the picture is up. */
		if (env->in_window != NULL) {
			r->window_hook_rc = env->in_window(env->in_window_ctx, &r->obs);
			if (r->window_hook_rc != 0)
				i915_show_anomaly(r, "the in-window test failed while the picture was up", r->window_hook_rc);
		}

		/* The finite window: every round wants 30 more frames (about half a second at 60 Hz) within 1.5 s. */
		r->steady_frame_first = r->frame_last;
		for (waited = 0U; r->window_hook_rc == 0 && waited < env->window_ms; waited += I915_LCD_STEADY_STEP_MS) {
			frame0 = 0U;
			r->steady_rc = drv_i915_lcd_observer_frames(&r->obs, I915_LCD_STEADY_ROUND_MS, I915_LCD_STEADY_FRAMES, &frame0, &r->steady_frame_last);
			drv_i915_lcd_observer_steady_sample(&r->obs);
			r->steady_rounds++;
			if (r->steady_rc != 0) {
				i915_show_anomaly(r, "the frame counter stopped advancing while the picture was up", r->steady_rc);
				break;
			}
		}

		/* The state at the end of the window. */
		drv_i915_lcd_observer_steady_end(&r->obs);
		drv_i915_xd_picture_status(pictures, I915_XD_PICTURE_RESIDENT, &r->at_window_end);
		drv_i915_xd_picture_link_status(pictures, I915_XD_PICTURE_RESIDENT, &r->at_window_end);

		/* A DP sink that lost the link while the picture was up. */
		if (!r->output_hdmi && r->steady_rc == 0) {
			if (!r->at_window_end.cr_ok || !r->at_window_end.eq_ok)
				i915_show_anomaly(r, "the sink lost the link while the picture was up", EIO);
		}

		/* An underrun while the picture stood. */
		if (r->obs.seen_steady != 0U)
			i915_show_anomaly(r, "underrun status during the steady picture", EIO);

		i915_show_reached(env, r, I915_LCD_SHOW_WINDOW_DONE);
	}

	/* Commit 2: the reference's stop path, whatever happened above. */
	disable_from = trace->n;
	drv_i915_lcd_trace_phase(trace, "commit: disable");
	r->disable_rc = drv_i915_xd_picture_dark(pictures, I915_XD_PICTURE_RESIDENT);
	drv_i915_xd_picture_status(pictures, I915_XD_PICTURE_RESIDENT, &r->at_disable);
	r->cleanup_errors = r->at_disable.errors - r->enable_errors;
	r->cleanup_first_error_trace_at = i915_show_first_error_after(trace, disable_from);
	if (r->disable_rc != I915_LCD_MS_OK)
		i915_show_anomaly(r, "the disable commit did not succeed", r->disable_rc);

	i915_show_reached(env, r, I915_LCD_SHOW_DISABLE_RETURNED);

	/* Has the display let go of the buffer?  Only when the hardware says the pipe stands. */
	r->stopped_rc = EBUSY;
	if (r->disable_rc == I915_LCD_MS_OK) {
		r->stopped_rc = drv_i915_lcd_observer_stopped(&r->obs, I915_LCD_STOP_WATCH_MS, &r->stop_frame_first, &r->stop_frame_last);

		/* Read at the pipe-disabled point, the well still on. */
		r->transconf_after_stop = r->obs.stop_transconf;
		if (r->stopped_rc != 0)
			i915_show_anomaly(r, "after the disable the pipe still reports activity", r->stopped_rc);
	}

	/* The stop is confirmed: the buffer is the caller's again. */
	if (r->disable_rc == I915_LCD_MS_OK && r->stopped_rc == 0) {
		i915_show_reached(env, r, I915_LCD_SHOW_STOP_CONFIRMED);
		drv_i915_xd_picture_released(pictures, I915_XD_PICTURE_RESIDENT);
		drv_i915_scanout_end(so);
		r->display_released = 1;
		if (verify != NULL)
			r->readback_bad_after = verify(verify_ctx, so);
	} else {
		/*
		 * Not confirmed: the backing pages, their DMA mapping, the GGTT
		 * entries, the pin and its owner stay for ever; the modeset object
		 * keeps what it holds (DC_OFF, power domains, DBUF).
		 */
		drv_i915_scanout_abandon(so);
		drv_i915_xd_picture_abandoned(pictures, I915_XD_PICTURE_RESIDENT);
		display->show_retained = 1;
		display->show_retained_hw = env->hw;
		r->abandoned = 1;
		i915_show_reached(env, r, I915_LCD_SHOW_ABANDONED);
	}

	/* The buffer was handed to the display. */
	return 1;
}

/* Judges a run: no anomaly, the picture up for the window, the link trained, no underrun, nothing unresolved. */
static int
i915_show_passed(
	struct i915_display *display,
	const struct i915_lcd_show_report *r,
	int released)
{
	/* The first anomaly, the in-window test and the release decide first. */
	if (r->first_anomaly != NULL)
		return 0;
	if (r->window_hook_rc != 0)
		return 0;
	if (!released)
		return 0;

	/* The enable and the window. */
	if (r->enable_rc != I915_LCD_MS_OK)
		return 0;
	if (r->steady_rc != 0)
		return 0;

	/* A DP sink must have reported a trained link after the enable. */
	if (!r->output_hdmi) {
		if (!r->at_enable.cr_ok || !r->at_enable.eq_ok)
			return 0;
	}

	/* No underrun in the steady picture, and the vblank interrupt never unmasked. */
	if (r->obs.seen_steady != 0U)
		return 0;
	if (r->obs.vblank_unmasked_seen != 0)
		return 0;

	/* The pixels after the stop, and a complete run log without unresolved steps. */
	if (r->readback_bad_after != 0U)
		return 0;
	if (display->show_trace.steps != 0U)
		return 0;
	if (display->show_trace.dropped != 0U)
		return 0;

	/* Passed. */
	return 1;
}

/* Starts a run's report, refusing when an earlier run's resources are retained. */
static int
i915_show_begin(
	struct i915_display *display,
	struct i915_lcd_show_env *env,
	struct i915_lcd_show_report *r)
{
	int retained;

	/* An earlier run left resources the display or the GPU may still use: nothing is touched. */
	retained = drv_i915_lcd_show_retained(display);
	if (retained)
		return EBUSY;

	retained = drv_i915_xd_picture_retained(&display->xd.pictures, I915_XD_PICTURE_RESIDENT);
	if (retained)
		return EBUSY;

	/* A fresh report of this run over the display's run log. */
	kern_memset(r, 0, sizeof(*r));
	r->output_hdmi = env->cfg.output_hdmi;
	r->first_error_trace_at = -1;
	r->cleanup_first_error_trace_at = -1;
	r->trace = &display->show_trace;

	/* Succeeded: the run may start. */
	return 0;
}

/* Returns the display a panel run belongs to (ctx: its struct i915_lcd_kernel). */
static struct i915_display *
i915_kernel_display(
	void *ctx)
{
	struct i915_lcd_kernel *k;

	/* A run that names its display (a second output's). */
	k = ctx;
	if (k->display != NULL)
		return k->display;

	/* Otherwise the run is the display's lk member. */
	return container_of(k, struct i915_display, lk);
}

/*
 * The register read hook of a panel run.  While the register trace is on
 * every access is printed; the evasion probe records the first scanline
 * the update body reads after its trigger.
 */
static uint32_t
i915_kernel_read32(
	void *ctx,
	uint32_t reg)
{
	struct i915_lcd_kernel *k;
	struct i915_display *display;
	uint32_t value;

	k = ctx;
	display = i915_kernel_display(ctx);

	/* Reads the register. */
	value = drv_i915_read32(k->d->mmio, reg);

	/* The register trace prints every access (a diagnostic switch). */
	if (display->i915_lcd_reg_trace)
		kern_logf("i915: takeover: read 0x%05x = 0x%08x\n", reg, value);

	/* The evasion probe (observation only): the first scanline read after the trigger. */
	if (k->probe_watch && reg == I915_LCD_PIPEDSL_A) {
		k->probe_first_dsl = value & I915_LCD_PIPEDSL_MASK;
		k->probe_watch = 0;
	}

	/* Reports the value. */
	return value;
}

/* The register write hook of a panel run. */
static void
i915_kernel_write32(
	void *ctx,
	uint32_t reg,
	uint32_t value)
{
	struct i915_lcd_kernel *k;
	struct i915_display *display;

	k = ctx;
	display = i915_kernel_display(ctx);

	/* The register trace prints every access (a diagnostic switch). */
	if (display->i915_lcd_reg_trace)
		kern_logf("i915: takeover: write 0x%05x = 0x%08x\n", reg, value);

	/* Writes the register. */
	drv_i915_write32(k->d->mmio, reg, value);
}

/* The read-modify-write hook of a panel run: returns the old value. */
static uint32_t
i915_kernel_rmw32(
	void *ctx,
	uint32_t reg,
	uint32_t clear,
	uint32_t set)
{
	struct i915_lcd_kernel *k;
	uint32_t old;

	k = ctx;

	/* Reads, clears, sets and writes back. */
	old = drv_i915_read32(k->d->mmio, reg);
	drv_i915_write32(k->d->mmio, reg, (old & ~clear) | set);

	/* Reports the value before the write. */
	return old;
}

/* The posting-read hook of a panel run. */
static void
i915_kernel_posting_read(
	void *ctx,
	uint32_t reg)
{
	struct i915_lcd_kernel *k;

	k = ctx;

	/* Reads the register and drops the value. */
	(void)drv_i915_read32(k->d->mmio, reg);
}

/*
 * The register wait hook of a panel run (intel_de_wait_for_register()):
 * 2 us of polling without sleeping, then the sleeping stage up to
 * timeout_ms.  Returns 0, the Linux ETIMEDOUT, or the Linux EIO for a time
 * base or wait primitive fault, which is also the run's anomaly.
 */
static int
i915_kernel_wait_reg(
	void *ctx,
	uint32_t reg,
	uint32_t mask,
	uint32_t value,
	unsigned timeout_ms)
{
	struct i915_lcd_kernel *k;
	uint32_t last;
	int error;

	k = ctx;
	last = 0U;

	/* Polls, then sleeps, until the masked register holds the value. */
	error = drv_i915_wait_reg(k->d->mmio, reg, mask, value, 2U, timeout_ms, &last);
	if (error == 0)
		return 0;

	/* The device did not reach the condition: the reference's timeout. */
	if (error == ETIMEDOUT) {
		k->wait_timeouts++;
		return I915_LCD_ETIMEDOUT;
	}

	/* Anything else is the time source failing: not a timeout, and the run's first anomaly. */
	k->time_faults++;
	drv_i915_lcd_backend_fault("time base / wait primitive fault in a register wait (not a timeout)\n");
	return I915_LCD_EIO;
}

/* The sleep hook of a panel run: the resident eDP's tick sleep. */
static void
i915_kernel_usleep(
	void *ctx,
	unsigned us)
{
	struct i915_lcd_kernel *k;
	unsigned before;

	k = ctx;

	/* Sleeps on the eDP's kernel backend and watches its time-fault count. */
	before = k->d->edp->k.time_faults;
	drv_i915_dp_kernel_sleep_us(&k->d->edp->k, us);

	/* A sleep that returned early or found the clock failed is the run's anomaly. */
	if (k->d->edp->k.time_faults != before) {
		k->time_faults++;
		drv_i915_lcd_backend_fault("time base fault during a sleep (returned early or the clock failed)\n");
	}
}

/* The short-delay hook of a panel run. */
static void
i915_kernel_udelay(
	void *ctx,
	unsigned us)
{
	struct i915_lcd_kernel *k;
	int error;

	k = ctx;

	/* Spins for the delay; a failed time base is the run's anomaly. */
	error = drv_i915_udelay(us);
	if (error != 0) {
		k->time_faults++;
		drv_i915_lcd_backend_fault("time base fault during a short delay\n");
	}
}

/* The DBUF hook of a panel run: requests exactly these slices through the display core. */
static void
i915_kernel_dbuf_slices_update(
	void *ctx,
	unsigned req_slices)
{
	struct i915_lcd_kernel *k;

	k = ctx;

	/* The same body the power-domain init uses. */
	drv_i915_gen9_dbuf_slices_update(k->d->dcore, (uint8_t)req_slices);
}

/* The lock hook of a panel run: takes or gives back the dpll or backlight mutex. */
static void
i915_kernel_lock(
	void *ctx,
	int which,
	int take)
{
	struct i915_lcd_kernel *k;
	struct i915_display *display;

	k = ctx;
	display = i915_kernel_display(ctx);

	/* Only the two modeset locks exist. */
	if (which < 0 || which > 1)
		return;

	/* The register trace names every lock step (a diagnostic switch). */
	if (display->i915_lcd_reg_trace) {
		if (take) {
			kern_logf("i915: takeover: lock %d take\n", which);
		} else {
			kern_logf("i915: takeover: lock %d give back\n", which);
		}
	}

	/* Takes or releases the mutex. */
	if (take) {
		mutex_lock(&k->locks[which]);
	} else {
		mutex_unlock(&k->locks[which]);
	}
}

/* The local_irq_disable() hook of the update section: remembers whether interrupts were on. */
static void
i915_kernel_irq_off(
	void *ctx)
{
	struct i915_lcd_kernel *k;

	k = ctx;

	/* Turns the CPU's interrupts off and keeps the state they were in. */
	k->irq_was_on = kern_irq_disable();
}

/* The local_irq_enable() hook of the update section: turns interrupts back on if they were. */
static void
i915_kernel_irq_on(
	void *ctx)
{
	struct i915_lcd_kernel *k;

	k = ctx;

	/* Interrupts that were off before the section stay off. */
	if (k->irq_was_on)
		kern_irq_enable();
}

/* Checks that pipe B and DDI B are idle for an HDMI run. */
static int
i915_kernel_preflight_hdmi(
	struct i915_lcd_kernel *k)
{
	const struct i915_lcd_kernel_deps *d;
	uint32_t transconf;
	uint32_t buf_ctl;

	d = k->d;

	/* Reads TRANSCONF(B) and DDI_BUF_CTL(B). */
	transconf = drv_i915_read32(d->mmio, I915_LCD_TRANSCONF_B);
	buf_ctl = drv_i915_read32(d->mmio, I915_LCD_DDI_BUF_CTL_B);

	/* The scanout buffer needs the GT memory's GGTT. */
	if (!d->gm->inited) {
		kern_logf("i915: HDMI-B preflight: no GT memory (GGTT) for the scanout buffer\n");
		return EBUSY;
	}

	/* Logs the pipe, the port, the south display status and both PLLs. */
	kern_logf("i915: HDMI-B preflight: TRANSCONF(B)=0x%08x DDI_BUF_CTL(B)=0x%08x SDEISR=0x%08x DPLL0=0x%08x DPLL1=0x%08x\n",
	    transconf,
	    buf_ctl,
	    drv_i915_read32(d->mmio, I915_LCD_SDEISR),
	    drv_i915_read32(d->mmio, I915_LCD_DPLL0_ENABLE),
	    drv_i915_read32(d->mmio, I915_LCD_DPLL1_ENABLE));

	/* Pipe B and DDI B must be off. */
	if ((transconf & I915_LCD_ENABLE_BIT) != 0U || (buf_ctl & I915_LCD_ENABLE_BIT) != 0U) {
		kern_logf("i915: HDMI-B preflight: pipe B / DDI B are not idle\n");
		return EBUSY;
	}

	/* Succeeded: the HDMI output may be written. */
	return 0;
}

/*
 * The observation window of the resident run: the serve loop runs until
 * the display is to be given back, then the panel goes back to buffer A,
 * the one the show body began with and will end.
 */
static int
i915_resident_window(
	void *ctx,
	struct i915_lcd_observer *o)
{
	struct i915_lcd_kernel *k;
	struct i915_display *display;
	int flip_error;

	UNUSED_PARAMETER(o);

	k = ctx;
	display = i915_kernel_display(ctx);

	/* The buffers are the GPU's to draw into from here on. */
	display->resident_up = 1;
	kern_logf("i915: resident display: picture up (buffer A surf 0x%08x); serving presentation\n", (uint32_t)display->resident_buf[0].surf);

	/* Serves until the display is to be given back. */
	(void)display->resident_serve(display->resident_serve_ctx);

	/* A flip armed without waiting completes before the buffers change hands (it logs a failure). */
	(void)drv_i915_xd_picture_flip_settle(&display->xd.pictures, I915_XD_PICTURE_RESIDENT);

	/*
	 * Lit again for two pipes next: buffer A, which the next
	 * lighting shows first, takes the picture B shows, so the output comes
	 * back with its last picture and not an older one.  The GPU wrote B
	 * through an uncached mapping: the CPU's stale lines go first (review
	 * F11).
	 */
	if (display->window.relight && display->resident_front != 0U) {
		drv_i915_gt_clflush(display->resident_buf[1].cpu, display->resident_buf[1].size);
		kern_memcpy(display->resident_buf[0].cpu, display->resident_buf[1].cpu, display->resident_buf[0].size);
		drv_i915_scanout_publish(&display->resident_buf[0]);
	}

	/* Ends on buffer A; the flip back is only armed, so it is settled too. */
	if (display->resident_front != 0U) {
		flip_error = drv_i915_lcd_resident_flip(display);
		if (flip_error != 0)
			kern_logf("i915: resident display: XXX could not flip back to buffer A before the stop\n");
		(void)drv_i915_xd_picture_flip_settle(&display->xd.pictures, I915_XD_PICTURE_RESIDENT);
	}

	/* The buffers are not the GPU's any more. */
	display->resident_up = 0;
	kern_logf("i915: resident display: released after %u flip(s); the reference stop path follows\n", k->flips_done);

	/* The window passed. */
	return 0;
}

/* The pictures are the application's: nothing to compare them with after the stop. */
static uint32_t
i915_resident_verify(
	void *ctx,
	const struct i915_scanout *so)
{
	UNUSED_PARAMETER(ctx);
	UNUSED_PARAMETER(so);

	/* No wrong pixels can be told. */
	return 0U;
}

/*
 * Creates and pins both resident buffers at the panel's size, black
 * outside the application's image, and publishes them.  A failure unpins
 * and destroys whatever was made.
 */
static int
i915_resident_buffers(
	struct i915_display *display,
	const struct i915_lcd_kernel_deps *d,
	const struct i915_lcd_state *lcd)
{
	struct i915_scanout *a;
	struct i915_scanout *b;
	int window_error;
	int error;
	int i;

	a = &display->resident_buf[0];
	b = &display->resident_buf[1];

	/* The display window of the GGTT; EBUSY means it was claimed earlier, which is fine. */
	window_error = drv_i915_gt_display_window_init(d->gm, I915_GT_DISPLAY_PAGES);
	if (window_error != 0 && window_error != EBUSY)
		return window_error;

	/* Creates and pins A at the output's size, then B at A's size. */
	error = drv_i915_scanout_create(d->gm, lcd->mode.hdisplay, lcd->mode.vdisplay, I915_FOURCC_XRGB8888, I915_MOD_LINEAR, a);
	if (error == 0)
		error = drv_i915_scanout_pin(a, "resident A");
	if (error == 0)
		error = drv_i915_scanout_create(d->gm, a->width, a->height, I915_FOURCC_XRGB8888, I915_MOD_LINEAR, b);
	if (error == 0)
		error = drv_i915_scanout_pin(b, "resident B");

	/* Gives back whatever was made, B first. */
	if (error != 0) {
		kern_logf("i915: resident display: not started (buffers rc=%d)\n", error);
		for (i = 1; i >= 0; i--) {
			(void)drv_i915_scanout_unpin(&display->resident_buf[i]);
			(void)drv_i915_scanout_destroy(&display->resident_buf[i]);
		}

		return error;
	}

	/* Black outside the application's image, visible to the display. */
	for (i = 0; i < 2; i++) {
		kern_memset(display->resident_buf[i].cpu, 0, display->resident_buf[i].size);
		drv_i915_scanout_publish(&display->resident_buf[i]);
	}

	/* Names both buffers: the kernel addresses let a debugger or the QEMU monitor read the picture. */
	kern_logf("i915: resident display: buffers %ux%u pitch %u: A surf 0x%08x cpu %p, B surf 0x%08x cpu %p\n",
	    a->width,
	    a->height,
	    a->pitch,
	    (uint32_t)a->surf,
	    (void *)a->cpu,
	    (uint32_t)b->surf,
	    (void *)b->cpu);

	/* Succeeded: both buffers are pinned and black. */
	return 0;
}

/*
 * Tells whether an earlier run kept both resident buffers pinned for this
 * one (the resident output lit again for two pipes, with its picture).  A keep that does not find both buffers pinned is forgotten.
 */
static int
i915_resident_buffers_kept(
	struct i915_display *display)
{
	/* Nothing was kept. */
	if (!display->window.keep_buffers)
		return 0;

	/* Both must be pinned and unused, as the earlier run left them. */
	if (display->resident_buf[0].state != I915_SCANOUT_PINNED ||
	    display->resident_buf[1].state != I915_SCANOUT_PINNED) {
		display->window.keep_buffers = 0;
		return 0;
	}

	/* Succeeded: the buffers are this run's. */
	return 1;
}

/*
 * Releases both resident buffers when the display provably reads neither,
 * or abandons them when it may.  Reports whether both were released.
 */
static int
i915_resident_release(
	struct i915_display *display,
	const struct i915_lcd_show_report *rep)
{
	struct i915_scanout *so;
	int error;
	int i;

	/* The display may still read the buffers: both are kept for ever. */
	if (!rep->display_released && rep->display_acquired) {
		for (i = 0; i < 2; i++) {
			so = &display->resident_buf[i];
			if (so->state >= I915_SCANOUT_PINNED && so->state != I915_SCANOUT_ABANDONED)
				drv_i915_scanout_abandon(so);
		}

		return 0;
	}

	/* The show body ended A; B's last use ends here. */
	drv_i915_scanout_end(&display->resident_buf[1]);

	/* Kept pinned, with their picture, for the lighting for two pipes that follows. */
	if (display->window.relight && display->window.keep_buffers)
		return 1;

	/* Unpins and destroys A then B, stopping at the first refusal. */
	error = drv_i915_scanout_unpin(&display->resident_buf[0]);
	if (error == 0)
		error = drv_i915_scanout_destroy(&display->resident_buf[0]);
	if (error == 0)
		error = drv_i915_scanout_unpin(&display->resident_buf[1]);
	if (error == 0)
		error = drv_i915_scanout_destroy(&display->resident_buf[1]);
	if (error != 0)
		return 0;

	/* Succeeded: both buffers are gone. */
	return 1;
}

/*
 * Judges the resident run: the show body's judgement without the complete
 * run log (a display that flips for as long as an application presents
 * always outgrows it); the counters keep counting past the last kept entry.
 */
static int
i915_resident_passed(
	const struct i915_lcd_show_report *rep)
{
	/* No anomaly, the window and the release. */
	if (rep->first_anomaly != NULL)
		return 0;
	if (rep->window_hook_rc != 0)
		return 0;
	if (!rep->display_released)
		return 0;

	/* The enable, the first frames and, on DP, the trained link (HDMI trains none). */
	if (rep->enable_rc != I915_LCD_MS_OK)
		return 0;
	if (rep->steady_rc != 0)
		return 0;
	if (!rep->output_hdmi) {
		if (!rep->at_enable.cr_ok || !rep->at_enable.eq_ok)
			return 0;
	}

	/* No underrun, the vblank interrupt never unmasked, and no wrong pixels. */
	if (rep->obs.seen_steady != 0U)
		return 0;
	if (rep->obs.vblank_unmasked_seen != 0)
		return 0;
	if (rep->readback_bad_after != 0U)
		return 0;

	/* The run log's counters: no unresolved step, no error, no timed-out wait. */
	if (rep->trace->steps != 0U)
		return 0;
	if (rep->trace->errors != 0U)
		return 0;
	if (rep->trace->wait_timeouts != 0U)
		return 0;

	/* Passed. */
	return 1;
}

/*
 * Takes over the display the firmware left running: every pipe the
 * firmware check found lit is stopped through the xd takeover
 * (drv_i915_xd_takeover), with the INIT power reference the probe kept
 * for it.  The PLL pool and the DBUF state start empty for the run's own
 * commit.  The firmware's framebuffer is never written.  Returns 0, or
 * the errno of the pipe that could not be stopped (logged).
 */
static int
i915_resident_takeover(
	struct i915_display *display,
	struct i915_lcd_kernel *k)
{
	struct i915_xd_takeover_outcome outcome;
	int error;

	UNUSED_PARAMETER(k);

	/* What the firmware left lit. */
	kern_logf("i915: takeover: lit pipes 0x%x, output %s (pipe %c, DDI %d)\n",
	    display->xd.firmware.lit_pipes,
	    drv_i915_xd_firmware_output_name(display->xd.firmware.output),
	    'A' + (int)display->xd.firmware.output_pipe,
	    display->xd.firmware.output_ddi);

	/* Stops every lit pipe. */
	error = drv_i915_xd_takeover(&display->xd, &outcome);
	kern_logf("i915: takeover: rc=%d, crtcs stopped %u, still active 0x%x\n",
	    error,
	    (unsigned)__builtin_popcount(outcome.stopped),
	    outcome.still_lit);
	if (error != 0)
		return error;

	/* The run's own commit starts from an empty PLL pool and DBUF state. */
	drv_i915_xd_pictures_forget(&display->xd.pictures);

	/* Succeeded: the firmware's display is stopped and the pipes are the driver's. */
	return 0;
}

/*
 * Fills the parameters of a resident run on the HDMI display: port B,
 * pipe and transcoder B, DPLL 0 from an empty pool, the chosen mode.
 */
static void
i915_resident_hdmi_params(
	const struct i915_display_output *output,
	struct i915_lcd_run_params *params)
{
	/* The HDMI output of DDI B, alone, with the mode output.c chose. */
	kern_memset(params, 0, sizeof(*params));
	params->output_hdmi = 1;
	params->port = I915_OUTPUT_HDMI_PORT;
	params->pipe = I915_OUTPUT_HDMI_PIPE;
	params->cpu_transcoder = I915_OUTPUT_HDMI_PIPE;
	params->dpll_id = 0;
	params->reset_dplls = 1;
	params->state = &output->state;
	params->tag = "resident HDMI";
}

/*
 * Turns a filled panel configuration into the HDMI encoder's: what
 * intel_ddi_init() would have left for port B, no backlight, and the VBT
 * child's HDMI level shift.
 */
static void
i915_resident_hdmi_cfg(
	struct i915_display *display,
	const struct i915_lcd_kernel_deps *d,
	const struct i915_display_output *output,
	struct i915_lcd_modeset_cfg *cfg)
{
	const struct i915_vbt_encoder *encoder;
	const struct i915_lcd_mode *mode;

	UNUSED_PARAMETER(display);

	/* Port B, pipe and transcoder B, DPLL 0, and the port's saved reversal and lane bits. */
	cfg->output_hdmi = 1;
	cfg->port = I915_OUTPUT_HDMI_PORT;
	cfg->pipe = I915_OUTPUT_HDMI_PIPE;
	cfg->cpu_transcoder = I915_OUTPUT_HDMI_PIPE;
	cfg->dpll_id = 0;
	cfg->aux_ch = I915_OUTPUT_HDMI_PORT;
	cfg->saved_port_bits = drv_i915_read32(d->mmio, I915_LCD_DDI_BUF_CTL_PORT_B) & I915_LCD_SAVED_PORT_BITS;

	/* An external display has no backlight of ours. */
	cfg->vbt_backlight_present = 0;

	/* The VBT child of the port: its HDMI level shift, or the table's default entry when it names none. */
	encoder = drv_i915_vbt_encoder_for_port(&d->edp->vbt->parsed, I915_OUTPUT_HDMI_PORT);
	cfg->vbt_hdmi_level_shift = -1;
	if (encoder != NULL)
		cfg->vbt_hdmi_level_shift = encoder->hdmi_level_shift;

	/* Logs what the run drives. */
	mode = &output->state.mode;
	kern_logf("i915: resident display: HDMI on port %d, pipe %d, DVI mode: %ux%u %d kHz | PLL cfgcr0=0x%08x cfgcr1=0x%08x | VBT level shift %d | saved DDI_BUF_CTL bits 0x%x\n",
	    cfg->port,
	    cfg->pipe,
	    mode->hdisplay,
	    mode->vdisplay,
	    mode->clock_khz,
	    output->state.pll.cfgcr0,
	    output->state.pll.cfgcr1,
	    cfg->vbt_hdmi_level_shift,
	    cfg->saved_port_bits);
}

/*
 * Fills the parameters of a resident run on an external DP display: the Type-C
 * port's DDI, pipe and transcoder B (as the HDMI output's), the port's TC PLL
 * from an empty pool, the prepared mode.
 */
static void
i915_resident_dp_ext_params(
	const struct i915_display_output *output,
	struct i915_lcd_run_params *params)
{
	/* The external DP output of the Type-C port, alone. */
	kern_memset(params, 0, sizeof(*params));
	params->output_dp_ext = 1;
	params->port = output->port;
	params->pipe = I915_OUTPUT_DP_EXT_PIPE;
	params->cpu_transcoder = I915_OUTPUT_DP_EXT_PIPE;
	params->dpll_id = (int)DPLL_ID_ICL_MGPLL1 + (int)output->tc_port;
	params->reset_dplls = 1;
	params->state = &output->state;
	params->tag = "resident DP";
}

/*
 * Turns a filled panel configuration into the external DP encoder's: what
 * intel_ddi_init() and intel_dp_init_connector() leave for a Type-C DDI with
 * DP -- the port, pipe B, the TC PLL, the port's AUX channel and its own DPCD
 * access, the sink's receiver capabilities from the claim's probe, no lane
 * reversal (only a legacy Type-C port may be reversed), no panel and no
 * backlight.
 */
static void
i915_resident_dp_ext_cfg(
	struct i915_display *display,
	const struct i915_lcd_kernel_deps *d,
	const struct i915_display_output *output,
	struct i915_lcd_modeset_cfg *cfg)
{
	const struct i915_lcd_mode *mode;
	const struct i915_lcd_link *link;

	UNUSED_PARAMETER(d);

	/* The Type-C port's DDI, pipe and transcoder B, the port's TC PLL. */
	cfg->output_hdmi = 0;
	cfg->output_dp_ext = 1;
	cfg->port = output->port;
	cfg->pipe = I915_OUTPUT_DP_EXT_PIPE;
	cfg->cpu_transcoder = I915_OUTPUT_DP_EXT_PIPE;
	cfg->dpll_id = (int)DPLL_ID_ICL_MGPLL1 + (int)output->tc_port;
	cfg->saved_port_bits = 0U;

	/* The port's AUX channel, and the sink reached over it. */
	cfg->aux_ch = display->tck.aux_ch[output->tc_port];
	cfg->aux_emit = drv_i915_dp_ext_aux_emit(display, output->port);
	kern_memcpy(cfg->dpcd, output->sink.dpcd, sizeof(cfg->dpcd));
	kern_memset(cfg->edp_dpcd, 0, sizeof(cfg->edp_dpcd));

	/* An external display has no panel power sequencer and no backlight of ours. */
	cfg->vbt_low_vswing = 0;
	cfg->vbt_hobl = 0;
	cfg->vbt_backlight_present = 0;
	cfg->vbt_hdmi_level_shift = -1;

	/* Logs what the run drives. */
	mode = &output->state.mode;
	link = &output->state.link;
	kern_logf("i915: resident display: DP on TC%u (port %d), pipe %d: %ux%u %d kHz | link %d kHz x%d %d bpp | TC PLL %d (div0 0x%08x) | DPCD rev 0x%02x branch %d\n",
	    output->tc_port + 1U,
	    cfg->port,
	    cfg->pipe,
	    mode->hdisplay,
	    mode->vdisplay,
	    mode->clock_khz,
	    link->rate_khz,
	    link->lanes,
	    link->bpp,
	    cfg->dpll_id,
	    output->state.pll.dkl.div0,
	    cfg->dpcd[0],
	    output->sink.branch);
}

/*
 * Checks that an external DP run's pipe B and the Type-C port's DDI are off
 * and that the GT memory is there for the scanout buffer: 0, or EBUSY.
 */
static int
i915_kernel_preflight_dp_ext(
	struct i915_lcd_kernel *k)
{
	const struct i915_lcd_kernel_deps *d;
	uint32_t transconf;
	uint32_t buf_ctl;
	uint32_t buf_ctl_reg;

	/* The run's dependencies; its parameters name the Type-C port's DDI (the resident output's or the second output's). */
	d = k->d;

	/* Reads TRANSCONF(B) and the port's DDI_BUF_CTL. */
	buf_ctl_reg = I915_LCD_DDI_BUF_CTL_A + I915_LCD_DDI_BUF_CTL_STRIDE * (uint32_t)k->p->port;
	transconf = drv_i915_read32(d->mmio, I915_LCD_TRANSCONF_B);
	buf_ctl = drv_i915_read32(d->mmio, buf_ctl_reg);

	/* The scanout buffer needs the GT memory's GGTT. */
	if (!d->gm->inited) {
		kern_logf("i915: DP-ext preflight: no GT memory (GGTT) for the scanout buffer\n");
		return EBUSY;
	}

	/* Logs the pipe and the port. */
	kern_logf("i915: DP-ext preflight: TRANSCONF(B)=0x%08x DDI_BUF_CTL(0x%05x)=0x%08x\n",
	    transconf,
	    buf_ctl_reg,
	    buf_ctl);

	/* Pipe B and the port's DDI must be off. */
	if ((transconf & I915_LCD_ENABLE_BIT) != 0U || (buf_ctl & I915_LCD_ENABLE_BIT) != 0U) {
		kern_logf("i915: DP-ext preflight: pipe B / the Type-C port's DDI are not idle\n");
		return EBUSY;
	}

	/* Succeeded: the external DP output may be written. */
	return 0;
}

/*
 * Lowers an external DP output's link after its link did not train, and
 * computes the output's link again for the next run (the Linux
 * intel_dp_get_link_train_fallback_values() and the modeset the retry work
 * asks for).  The output is the resident one or the second
 * output.  Only an enable that reported the link as not
 * trained is retried.  Returns 0 when the next run may try the lower link,
 * ENOSPC when no lower link carries the mode, or EINVAL for a run that did
 * not fail at the link.
 */
static int
i915_resident_dp_ext_fallback(
	struct i915_display *display,
	struct i915_display_output *output,
	int enable_rc)
{
	struct i915_lcd_state next;
	struct i915_lcd_link *link;
	unsigned long irq;
	int max_rate;
	int max_lanes;
	int error;

	/* Only a link that did not train is retried. */
	if (enable_rc != I915_LCD_MS_LINK_NOT_TRAINED)
		return EINVAL;

	/* Lowers the port's limits below the link that failed. */
	link = &output->state.link;
	error = drv_i915_dp_ext_link_fallback(display, output->port, link->rate_khz, link->lanes);
	if (error != 0)
		return ENOSPC;

	/* The limits now in force. */
	error = drv_i915_dp_ext_link_limits(display, output->port, &max_rate, &max_lanes);
	if (error != 0)
		return ENOSPC;

	/* The mode's link under them, computed apart so a refusal leaves the output as it was. */
	error = drv_i915_lcd_compute_dp_ext(&output->state.mode, &output->sink, max_rate, max_lanes, output->state.pll.ref_khz, &next);
	if (error != 0) {
		kern_logf("i915: resident display: DP on TC%u: no lower link carries the mode (%d)\n", output->tc_port + 1U, error);
		return ENOSPC;
	}

	/* Logs the retry. */
	kern_logf("i915: resident display: DP on TC%u: retrying at %d kHz x%d (was %d kHz x%d)\n",
	    output->tc_port + 1U,
	    next.link.rate_khz,
	    next.link.lanes,
	    link->rate_khz,
	    link->lanes);

	/* The next run drives the lower link, under the lock a change of output takes. */
	irq = spin_lock_irqsave(&display->device->irq_lock);

	output->state = next;

	spin_unlock_irqrestore(&display->device->irq_lock, irq);

	/* Succeeded: the run may be tried again. */
	return 0;
}

/*
 * Says how the resident run lights an output of a kind: the one place that
 * chooses among the kinds' parameters and configurations.
 * Returns 0 with the way (all empty for the panel), or EOPNOTSUPP for a
 * kind this driver does not light.
 */
static int
i915_resident_output_way(
	enum i915_output_kind kind,
	struct i915_resident_output_way *way)
{
	/* Nothing to change for the panel. */
	kern_memset(way, 0, sizeof(*way));

	/* Each kind's way. */
	switch (kind) {
	case I915_OUTPUT_KIND_PANEL:
		break;
	case I915_OUTPUT_KIND_HDMI:
		/* DDI B, pipe B, DVI mode, the PLL pool its own. */
		way->params = i915_resident_hdmi_params;
		way->cfg = i915_resident_hdmi_cfg;
		way->own_pool = 1;
		break;
	case I915_OUTPUT_KIND_DP_EXT:
		/* A Type-C port's DDI, pipe B, its TC PLL, the PLL pool its own. */
		way->params = i915_resident_dp_ext_params;
		way->cfg = i915_resident_dp_ext_cfg;
		way->own_pool = 1;
		break;
	default:
		return EOPNOTSUPP;
	}

	/* Succeeded: the way is known. */
	return 0;
}
