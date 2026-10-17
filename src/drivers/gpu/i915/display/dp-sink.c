/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The eDP sink: panel power sequencer ownership and the AUX channel, up to
 * the sink's DPCD and EDID.
 *
 * This file owns the objects the Linux text of aux.c, panel.c and
 * edid-read.c works on (one digital port, DP output and connector: a single
 * live eDP, kept in the DP environment's world), supplies the environment
 * hooks dp-internal.h declares, and calls the Linux functions in the order
 * of intel_edp_init_connector() and intel_edp_init_dpcd():
 *
 *   drv_i915_pps_init()            PPS pick, delays (BIOS / VBT / spec), registers, VDD adopt
 *   drm_dp_read_dpcd_caps()        every AUX transfer takes the PPS lock, forces VDD on
 *   drm_dp_dpcd_read(DP_EDP_DPCD_REV)  and keeps it on while the PPS is initializing
 *   the EDID over the AUX DDC adapter
 *   drv_i915_pps_init_late()       final delays; schedules the delayed VDD-off
 *   drv_i915_pps_vdd_off_sync()    the stop path (and Linux's out_vdd_off)
 *
 * Not done here, and said so rather than implied: intel_hpd_enable_detection(),
 * the shared-AUX HPD check, drm_dp_read_desc() and the DPCD quirks, the PSR,
 * DSC and MSO capabilities, the sink-rate tables, the mode construction and
 * the backlight set-up.
 *
 * It also holds the DPCD and I2C-over-AUX helpers of the Linux v6.8.12
 * drivers/gpu/drm/display/drm_dp_helper.c (sha256
 * 030568524ac5db3fbd09725df196b22a430ec18fc1432dbb952298ce7c791a73):
 * drm_dp_dpcd_access() and the functions built on it, and the I2C-over-AUX
 * algorithm the EDID is read through, with the defaults of the two module
 * parameters (10 kHz, 16 bytes) fixed.
 *
 * And it binds the environment to the real kernel and GPU: MMIO, register
 * waits, tick-driven sleeps, real mutexes, a timer and worker thread for the
 * delayed VDD-off and the asynchronous power put, and the resident eDP
 * device that the output setup builds and the device stop releases.
 *
 * The messages of the Linux text show the format text only and never
 * evaluate their arguments (dp-internal.h).
 *
 * The drm_dp_helper.c original carries this notice:
 *
 * Copyright (C) 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "dp-internal.h"
#include <kern/clock.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <uapi/errno.h>
#include "../mmio.h"
#include "../sync.h"
#include "../workqueue.h"
#include "dp-sink.h"
#include "power.h"
#include "vbt-parse.h"
#include <kern/kcrt.h>

/* The retry interval of the AUX helpers, in microseconds. */
#define AUX_RETRY_INTERVAL 500

/* The bit times of the parts of an AUX request and reply (1 MHz AUX clock). */
#define AUX_PRECHARGE_LEN 10 /* 10 to 16 */
#define AUX_SYNC_LEN (16 + 4) /* preamble + AUX_SYNC_END */
#define AUX_STOP_LEN 4
#define AUX_CMD_LEN 4
#define AUX_ADDRESS_LEN 20
#define AUX_REPLY_PAD_LEN 4
#define AUX_LENGTH_LEN 8

/* The bit times of the parts of an I2C transfer. */
#define I2C_START_LEN 1
#define I2C_STOP_LEN 1
#define I2C_ADDR_LEN 9 /* ADDRESS + R/W + ACK/NACK */
#define I2C_DATA_LEN 9 /* DATA + ACK/NACK */

/*
 * The I2C bus speed the retry count assumes, in kHz (the Linux
 * dp_aux_i2c_speed_khz module parameter, fixed at its default).
 *
 * 10 kHz, because some real devices seem to need it; the speed is not
 * queried from the DPCD.
 */
#define I915_DP_AUX_I2C_SPEED_KHZ 10

/*
 * The largest I2C-over-AUX transfer (the Linux dp_aux_i2c_transfer_size
 * module parameter, fixed at its default).
 *
 * The maximum, because Bizlink DP-to-DVI-D dual-link adapters need
 * transfers as large as possible.
 */
#define I915_DP_AUX_I2C_TRANSFER_SIZE DP_AUX_MAX_PAYLOAD_BYTES

/* The DDI_BUF_CTL register of port A, which the eDP's saved port bits come from. */
#define I915_EDP_DDI_BUF_CTL_A 0x64000u

/* The DDI_BUF_CTL port-reversal bit that intel_ddi_init() keeps (DDI_BUF_PORT_REVERSAL). */
#define I915_EDP_DDI_BUF_PORT_REVERSAL 0x00010000u

/*
 * The world whose eDP the Linux text's argument-less helpers reach.
 *
 * The Linux text asks for a message, a sleep, the time or a delayed-work
 * operation without naming a device (msleep(), jiffies, drm_dbg_kms() and
 * the others), so those helpers find the DP world here.  Only one display,
 * and so one eDP, exists: the world is recorded when the display creates it
 * and forgotten when the display destroys it.  NULL means no DP world; the
 * helpers then do nothing, as they do while no eDP is live.  It is written
 * by the device start and stop only, which run one at a time.
 */
static struct i915_dp_world *i915_dp_current_world;

/*
 * The algorithm the DDC adapter of an AUX channel runs I2C over AUX with.
 *
 * It is constant; every AUX channel's adapter points at it from
 * drv_i915_drm_dp_aux_init() on.
 */
static u32 i915_drm_dp_i2c_functionality(struct i2c_adapter *adapter);
static int i915_drm_dp_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num);
static const struct i2c_algorithm i915_drm_dp_i2c_algo = {
	i915_drm_dp_i2c_xfer,
	i915_drm_dp_i2c_functionality,
};

static int i915_dp_power_slot(int domain);
static void i915_drm_dp_dump_access(const struct drm_dp_aux *aux, u8 request, uint offset, void *buffer, int ret);
static int i915_drm_dp_dpcd_transact(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg, size_t size);
static int i915_drm_dp_dpcd_access(struct drm_dp_aux *aux, u8 request, unsigned int offset, void *buffer, size_t size);
static int i915_drm_dp_dpcd_probe(struct drm_dp_aux *aux, unsigned int offset);
static i915_dp_ssize_t i915_drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int offset, void *buffer, size_t size);
static i915_dp_ssize_t i915_drm_dp_dpcd_write(struct drm_dp_aux *aux, unsigned int offset, void *buffer, size_t size);
static int i915_drm_dp_read_extended_dpcd_caps(struct drm_dp_aux *aux, u8 dpcd[DP_RECEIVER_CAP_SIZE]);
static int i915_drm_dp_read_dpcd_caps(struct drm_dp_aux *aux, u8 dpcd[DP_RECEIVER_CAP_SIZE]);
static void i915_drm_dp_i2c_msg_write_status_update(struct drm_dp_aux_msg *msg);
static int i915_drm_dp_aux_req_duration(const struct drm_dp_aux_msg *msg);
static int i915_drm_dp_aux_reply_duration(const struct drm_dp_aux_msg *msg);
static int i915_drm_dp_i2c_msg_duration(const struct drm_dp_aux_msg *msg, int i2c_speed_khz);
static int i915_drm_dp_i2c_retry_count(const struct drm_dp_aux_msg *msg, int i2c_speed_khz);
static int i915_drm_dp_i2c_do_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg);
static void i915_drm_dp_i2c_msg_set_request(struct drm_dp_aux_msg *msg, const struct i2c_msg *i2c_msg);
static int i915_drm_dp_i2c_drain_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *orig_msg);

/*
 * Creates the DP environment's world of a display.
 *
 * The world starts empty: no eDP is live and only error messages are shown.
 * Returns 0, or ENOMEM.
 */
int
drv_i915_dp_world_create(
	struct i915_display *display)
{
	struct i915_dp_world *world;

	/* Allocates the world cleared. */
	world = kern_calloc(1U, sizeof(*world));
	if (world == NULL)
		return ENOMEM;

	/*
	 * Publishes the world to the display and to the Linux text's
	 * argument-less helpers.
	 */
	display->dp_world = world;
	i915_dp_current_world = world;

	/* Succeeded: the display owns the world. */
	return 0;
}

/*
 * Destroys the DP environment's world of a display.
 *
 * The eDP must have ended (drv_i915_edp_device_fini()).  A display without a
 * world is left alone.
 */
void
drv_i915_dp_world_destroy(
	struct i915_display *display)
{
	struct i915_dp_world *world;

	/* Nothing to destroy without a world. */
	world = display->dp_world;
	if (world == NULL)
		return;

	/* Withdraws the world from the helpers and the display, then frees it. */
	if (i915_dp_current_world == world)
		i915_dp_current_world = NULL;

	display->dp_world = NULL;
	kern_free(world);
}

/*
 * Stops the external DP ports of the display an eDP device belongs to,
 * before the device's backend goes.
 */
void
drv_i915_dp_ext_stop_of(
	struct i915_edp_device *dev)
{
	struct i915_display *display;

	/* The eDP device is embedded in its display, whose DP world holds the ports. */
	display = container_of(dev, struct i915_display, edp_dev);
	drv_i915_dp_ext_stop(display->dp_world);
}

/*
 * Counts an error-level DP message and tells whether a message of a level
 * is shown.
 */
int
drv_i915_dp_log_enabled(
	int level)
{
	struct i915_dp_world *world;

	/* Without a world only errors are shown, and nothing is counted. */
	world = i915_dp_current_world;
	if (world == NULL) {
		if (level <= I915_VBT_LOG_ERR)
			return 1;

		return 0;
	}

	/* Counts an error; the count goes into the next snapshot. */
	if (level == I915_VBT_LOG_ERR)
		world->dp_log_errors++;

	/* Reports a level more verbose than the one configured. */
	if (level > world->dp_log_level)
		return 0;

	/* Succeeded: the level is shown. */
	return 1;
}

/*
 * Shows one DP message whose level is shown (drv_i915_dp_log_enabled()
 * counted it): formatted with the kernel's printf subset, where a
 * conversion outside it (Linux's %*ph, a precision) is shown as written and
 * its argument consumed, after the DP's prefix.
 */
void
drv_i915_dp_notef(
	int level,
	const char *fmt,
	...)
{
	char text[256];
	va_list arguments;

	/* Renders the message, cut to the room. */
	va_start(arguments, fmt);
	(void)kern_vsnprintf(text, sizeof(text), fmt, arguments);
	va_end(arguments);

	/* Writes the prefix, marking an error, then the message. */
	if (level == I915_VBT_LOG_ERR) {
		kern_logf("i915: dp [err] %s", text);
	} else {
		kern_logf("i915: dp %s", text);
	}
}

/*
 * Reports the environment the argument-less helpers use: the live eDP's,
 * else the external DP ports' once they started, else NULL.
 */
struct i915_dp_env *
drv_i915_dp_env_current(void)
{
	struct i915_dp_world *world;

	/* No world: no eDP. */
	world = i915_dp_current_world;
	if (world == NULL)
		return NULL;

	/* A live eDP's environment serves the helpers. */
	if (world->edp.live)
		return world->edp.env;

	/* Without one, the external ports' environment does (the same backend). */
	if (world->ext.live)
		return &world->ext.env;

	/* Nothing is live: no environment. */
	return NULL;
}

/*
 * Sleeps through the live eDP's environment (msleep() and usleep_range()
 * of the Linux text).
 *
 * The sleep is counted in the environment; without a live eDP nothing
 * happens.
 */
void
drv_i915_dp_sleep_us(
	unsigned us)
{
	struct i915_dp_env *env;

	/* Nothing to sleep through without a live eDP. */
	env = drv_i915_dp_env_current();
	if (env == NULL)
		return;

	/* Counts the sleep atomically: the worker thread sleeps too. */
	(void)__atomic_add_fetch(&env->sleeps, 1u, __ATOMIC_SEQ_CST);
	(void)__atomic_add_fetch(&env->slept_us, (uint64_t)us, __ATOMIC_SEQ_CST);

	/* Sleeps. */
	env->sleep_us(env->ctx, us);
}

/*
 * Reports the live eDP's clock in milliseconds (jiffies and ktime of the
 * Linux text), or 0 without a live eDP.
 */
u64
drv_i915_dp_now_ms(void)
{
	struct i915_dp_env *env;
	u64 now;

	/* No clock without a live eDP. */
	env = drv_i915_dp_env_current();
	if (env == NULL)
		return 0;

	/* Reads the environment's clock. */
	now = env->now_ms(env->ctx);

	/* Succeeded: reports the time. */
	return now;
}

/*
 * Takes a power-domain reference through the device's environment (the
 * Linux intel_display_power_get()).
 *
 * It returns 1, or -1 when the environment refused; a refusal cannot be
 * handled by the Linux text, so the transfer that follows times out and
 * says so.
 */
intel_wakeref_t
drv_i915_dp_power_get(
	struct drm_i915_private *i915,
	int domain)
{
	struct i915_dp_env *env;
	int error;
	int slot;

	/* Takes the reference through the environment. */
	env = i915->dp_env;
	error = env->power_get(env->ctx, domain);
	if (error != 0) {
		env->power_get_failures++;
		I915_DP_LOG(I915_VBT_LOG_ERR, "display power get failed\n");
		return -1;
	}

	/* Counts the reference the DP text now holds on the domain. */
	slot = i915_dp_power_slot(domain);
	(void)__atomic_add_fetch(&env->power_refs[slot], 1, __ATOMIC_SEQ_CST);

	/* Succeeded: the caller holds one reference. */
	return 1;
}

/*
 * Returns a power-domain reference through the device's environment (the
 * Linux intel_display_power_put()).
 *
 * A reference of 0 (nothing held) or -1 (the get had failed) is ignored; a
 * put without a counted reference is reported and not passed on.
 */
void
drv_i915_dp_power_put(
	struct drm_i915_private *i915,
	int domain,
	intel_wakeref_t wakeref)
{
	struct i915_dp_env *env;
	int slot;

	/* Finds the environment and the domain's counter. */
	env = i915->dp_env;
	slot = i915_dp_power_slot(domain);

	/* Nothing held: nothing to return. */
	if (wakeref <= 0)
		return;

	/* Refuses a put the DP text holds no reference for. */
	if (env->power_refs[slot] <= 0) {
		env->power_put_underflows++;
		I915_DP_LOG(I915_VBT_LOG_ERR, "display power put without a reference\n");
		return;
	}

	/* Uncounts the reference and returns it. */
	(void)__atomic_sub_fetch(&env->power_refs[slot], 1, __ATOMIC_SEQ_CST);
	env->power_put(env->ctx, domain);
}

/*
 * Hands a power-domain reference to the power layer, which releases it
 * later (the Linux intel_display_power_put_async()).
 *
 * The power layer parks it, gives it back to the next get of the domain, or
 * releases it about 100 ms later.
 */
void
drv_i915_dp_power_put_async(
	struct drm_i915_private *i915,
	int domain,
	intel_wakeref_t wakeref)
{
	struct i915_dp_env *env;
	int slot;

	/* Finds the environment and the domain's counter. */
	env = i915->dp_env;
	slot = i915_dp_power_slot(domain);

	/* Nothing held: nothing to hand over. */
	if (wakeref <= 0)
		return;

	/* Refuses a put the DP text holds no reference for. */
	if (env->power_refs[slot] <= 0) {
		env->power_put_underflows++;
		I915_DP_LOG(I915_VBT_LOG_ERR, "display power put_async without a reference\n");
		return;
	}

	/* The reference leaves the DP text here: uncounts it, counts the hand-over, and passes it on. */
	(void)__atomic_sub_fetch(&env->power_refs[slot], 1, __ATOMIC_SEQ_CST);
	(void)__atomic_add_fetch(&env->async_puts, 1u, __ATOMIC_SEQ_CST);
	env->power_put_async(env->ctx, domain);
}

/*
 * Takes a PPS or AUX mutex (the Linux mutex_lock()).
 *
 * The environment's lock gives the exclusion; the held mark serves the
 * Linux lock assertions, and a mutex taken while marked held is reported.
 */
void
drv_i915_dp_mutex_lock(
	struct i915_dp_mutex *m,
	const char *name)
{
	/* Blocks on the environment's lock. */
	if (m->env != NULL)
		m->env->lock(m->env->ctx, m->id);

	/* Reports a recursion or a lost unlock. */
	if (m->held) {
		if (m->env != NULL)
			m->env->lock_errors++;

		I915_DP_LOG(I915_VBT_LOG_ERR, "mutex %s: taken while marked held\n", name);
	}

	/* Marks the mutex held for the Linux lock assertions, and counts the take. */
	m->held = 1;
	m->acquisitions++;
}

/*
 * Drops a PPS or AUX mutex (the Linux mutex_unlock()).
 *
 * An unlock of a mutex that is not marked held is reported.
 */
void
drv_i915_dp_mutex_unlock(
	struct i915_dp_mutex *m,
	const char *name)
{
	/* Reports an unlock of a free mutex. */
	if (!m->held) {
		if (m->env != NULL)
			m->env->lock_errors++;

		I915_DP_LOG(I915_VBT_LOG_ERR, "mutex %s: unlock while free\n", name);
	}

	/* The mutex is no longer held by the Linux text. */
	m->held = 0;

	/* Releases the environment's lock. */
	if (m->env != NULL)
		m->env->unlock(m->env->ctx, m->id);
}

/*
 * Queues a delayed work through the live eDP's environment (the Linux
 * queue_delayed_work()).
 *
 * It reports whether the work was newly queued; nothing is queued without a
 * live eDP.
 */
bool
drv_i915_dp_delayed_queue(
	struct delayed_work *dw,
	unsigned long delay_ms)
{
	struct i915_dp_env *env;
	int queued;

	/* Nothing to queue on without a live eDP. */
	env = drv_i915_dp_env_current();
	if (env == NULL)
		return false;

	/* Queues the work in its slot. */
	queued = env->delayed_queue(env->ctx, dw->slot, (unsigned)delay_ms);
	if (queued == 0)
		return false;

	/* Succeeded: the work is newly queued. */
	return true;
}

/*
 * Cancels a delayed work through the live eDP's environment (the Linux
 * cancel_delayed_work() and cancel_delayed_work_sync()).
 *
 * With sync set it also waits for a running body.  It reports whether the
 * work was pending; nothing is cancelled without a live eDP.
 */
bool
drv_i915_dp_delayed_cancel(
	struct delayed_work *dw,
	int sync)
{
	struct i915_dp_env *env;
	int cancelled;

	/* Nothing to cancel without a live eDP. */
	env = drv_i915_dp_env_current();
	if (env == NULL)
		return false;

	/* Cancels the work in its slot. */
	cancelled = env->delayed_cancel(env->ctx, dw->slot, sync);
	if (cancelled == 0)
		return false;

	/* Succeeded: the pending work was cancelled. */
	return true;
}

/*
 * Prepares an AUX channel for transfers (the Linux drm_dp_aux_init()).
 *
 * The hardware mutex is made free and the DDC adapter is bound to the
 * I2C-over-AUX algorithm.  The I2C core's own -EAGAIN retry loop is not
 * reproduced: the I2C-over-AUX transfer never returns -EAGAIN.
 */
void
drv_i915_drm_dp_aux_init(
	struct drm_dp_aux *aux)
{
	/* A free, never-taken hardware mutex. */
	i915_dp_mutex_init(&aux->hw_mutex);

	/* The DDC adapter runs I2C over this channel, retried three times. */
	aux->ddc.algo = &i915_drm_dp_i2c_algo;
	aux->ddc.algo_data = aux;
	aux->ddc.retries = 3;
}

/*
 * Reads DPCD bytes over any AUX channel (the Linux drm_dp_dpcd_read()), for
 * the external DP ports.
 *
 * It returns the bytes transferred, or a negative Linux errno.
 */
long
drv_i915_drm_dp_dpcd_read(
	struct drm_dp_aux *aux,
	unsigned offset,
	void *buffer,
	size_t size)
{
	long transferred;

	/* Reads the bytes. */
	transferred = i915_drm_dp_dpcd_read(aux, offset, buffer, size);
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes read. */
	return transferred;
}

/*
 * Writes DPCD bytes over any AUX channel (the Linux drm_dp_dpcd_write()),
 * as drv_i915_drm_dp_dpcd_read().
 */
long
drv_i915_drm_dp_dpcd_write(
	struct drm_dp_aux *aux,
	unsigned offset,
	const void *buffer,
	size_t size)
{
	long transferred;

	/* Writes the bytes; the Linux helper takes a writable buffer but only reads it. */
	transferred = i915_drm_dp_dpcd_write(aux, offset, (void *)buffer, size);
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes written. */
	return transferred;
}

/*
 * Wakes a sink with a throw-away read (the Linux drm_dp_dpcd_probe()).
 *
 * It returns 0, or a negative Linux errno.
 */
int
drv_i915_drm_dp_dpcd_probe(
	struct drm_dp_aux *aux,
	unsigned offset)
{
	int error;

	/* Reads one byte and throws it away. */
	error = i915_drm_dp_dpcd_probe(aux, offset);
	if (error != 0)
		return error;

	/* Succeeded: the sink answered. */
	return 0;
}

/*
 * Reads a sink's receiver capabilities, with the extended ones where it
 * has them (the Linux drm_dp_read_dpcd_caps()).
 *
 * It returns 0, or a negative Linux errno.
 */
int
drv_i915_drm_dp_read_dpcd_caps(
	struct drm_dp_aux *aux,
	u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int error;

	/* Reads the capabilities. */
	error = i915_drm_dp_read_dpcd_caps(aux, dpcd);
	if (error != 0)
		return error;

	/* Succeeded: the capabilities are in dpcd. */
	return 0;
}

/* Reports the power-reference counter of a domain: 0 for DISPLAY_CORE, 1 for the AUX domain. */
static int
i915_dp_power_slot(
	int domain)
{
	/* The display core has a counter of its own. */
	if (domain == POWER_DOMAIN_DISPLAY_CORE)
		return 0;

	/* Succeeded: every other domain is the AUX domain. */
	return 1;
}

/* Reports one DPCD access at debug level (the Linux drm_dp_dump_access()). */
static void
i915_drm_dp_dump_access(
	const struct drm_dp_aux *aux,
	u8 request,
	uint offset,
	void *buffer,
	int ret)
{
	const char *arrow;

	/* A read points away from the sink, a write towards it. */
	if (request == DP_AUX_NATIVE_READ) {
		arrow = "->";
	} else {
		arrow = "<-";
	}

	/* Reports the access, with its first bytes when some were transferred. */
	if (ret > 0) {
		drm_dbg_dp(aux->drm_dev, "%s: 0x%05x AUX %s (ret=%3d) %*ph\n",
			   aux->name, offset, arrow, ret, min(ret, 20), buffer);
	} else {
		drm_dbg_dp(aux->drm_dev, "%s: 0x%05x AUX %s (ret=%3d)\n",
			   aux->name, offset, arrow, ret);
	}
}

/*
 * Runs one native AUX transaction with retries, the hardware mutex held.
 *
 * The DP specification does not say how often to retry a native
 * transaction; 32 tries make Dell 4k monitors happier than the 7 of I2C.  A
 * failure other than a timeout waits the retry interval before the next
 * try.  It returns size when the whole transfer was acknowledged; otherwise
 * the error of the first try, because a later try may fail differently:
 * -EBUSY for a powered-down sink, -EPROTO for a short transfer, -EIO for a
 * refused one, or the transfer hook's error.
 */
static int
i915_drm_dp_dpcd_transact(
	struct drm_dp_aux *aux,
	struct drm_dp_aux_msg *msg,
	size_t size)
{
	unsigned int retry;
	unsigned int native_reply;
	int err;
	int ret;
	int done;

	/* A powered-down sink is not worth a transfer. */
	if (aux->powered_down)
		return -I915_DP_EBUSY;

	/* Tries the transaction up to 32 times. */
	err = 0;
	ret = 0;
	done = 0;
	for (retry = 0; retry < 32; retry++) {
		/* A failure other than a timeout waits before the next try. */
		if (ret != 0 && ret != -I915_DP_ETIMEDOUT)
			i915_dp_usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);

		/* Runs the transaction; an acknowledged whole transfer ends the retries. */
		ret = aux->transfer(aux, msg);
		if (ret >= 0) {
			native_reply = msg->reply & DP_AUX_NATIVE_REPLY_MASK;
			if (native_reply == DP_AUX_NATIVE_REPLY_ACK) {
				if ((size_t)ret == size) {
					done = 1;
					break;
				}

				ret = -I915_DP_EPROTO;
			} else {
				ret = -I915_DP_EIO;
			}
		}

		/* Keeps the error of the first try. */
		if (!err)
			err = ret;
	}

	/* Reports the first try's error when no try succeeded. */
	if (!done) {
		I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: Too many retries, giving up. First error: %d\n",
				    aux->name, err);
		return err;
	}

	/* Succeeded: the whole transfer was acknowledged. */
	return ret;
}

/*
 * Runs one native AUX transaction with retries (the Linux
 * drm_dp_dpcd_access()).
 *
 * The channel's hardware mutex is held around the tries.  It returns the
 * bytes transferred, or a negative Linux errno.
 */
static int
i915_drm_dp_dpcd_access(
	struct drm_dp_aux *aux,
	u8 request,
	unsigned int offset,
	void *buffer,
	size_t size)
{
	struct drm_dp_aux_msg msg;
	int ret;

	/* Describes the transaction. */
	kern_memset(&msg, 0, sizeof(msg));
	msg.address = offset;
	msg.request = request;
	msg.buffer = buffer;
	msg.size = size;

	/* Runs the tries with the channel held. */
	I915_DP_MUTEX_LOCK(&aux->hw_mutex);

	ret = i915_drm_dp_dpcd_transact(aux, &msg, size);

	I915_DP_MUTEX_UNLOCK(&aux->hw_mutex);

	/* Reports a failed transaction. */
	if (ret < 0)
		return ret;

	/* Succeeded: reports the bytes transferred. */
	return ret;
}

/*
 * Reads one DPCD byte to wake the sink, throwing the value away (the Linux
 * drm_dp_dpcd_probe()).
 *
 * It returns 0, or a negative Linux errno.
 */
static int
i915_drm_dp_dpcd_probe(
	struct drm_dp_aux *aux,
	unsigned int offset)
{
	u8 buffer;
	int ret;

	/* Reads the byte; a zero-length answer is a helper bug. */
	ret = i915_drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, offset, &buffer, 1);
	(void)I915_DP_WARN_ON(ret == 0);

	i915_drm_dp_dump_access(aux, DP_AUX_NATIVE_READ, offset, &buffer, ret);

	/* Reports a failed read. */
	if (ret < 0)
		return ret;

	/* Succeeded: the sink answered. */
	return 0;
}

/*
 * Reads DPCD bytes (the Linux drm_dp_dpcd_read()).
 *
 * A throw-away read of DP_DPCD_REV comes first: the HP ZR24w corrupts the
 * first DPCD access after entering power save, and there is no better place
 * for the workaround than before every access.  It returns the bytes
 * transferred, or a negative Linux errno: -EIO for a refused transfer or too
 * many retries, -EPROTO for a short one, and the transfer hook's error
 * otherwise.
 */
static i915_dp_ssize_t
i915_drm_dp_dpcd_read(
	struct drm_dp_aux *aux,
	unsigned int offset,
	void *buffer,
	size_t size)
{
	int ret;

	/* Wakes the sink with a throw-away read. */
	if (!aux->is_remote) {
		ret = i915_drm_dp_dpcd_probe(aux, DP_DPCD_REV);
		if (ret < 0)
			return ret;
	}

	/* Reads the bytes. */
	if (aux->is_remote) {
		ret = drm_dp_mst_dpcd_read(aux, offset, buffer, size);
	} else {
		ret = i915_drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, offset, buffer, size);
	}

	i915_drm_dp_dump_access(aux, DP_AUX_NATIVE_READ, offset, buffer, ret);

	/* Reports a failed read. */
	if (ret < 0)
		return ret;

	/* Succeeded: reports the bytes read. */
	return ret;
}

/*
 * Writes DPCD bytes (the Linux drm_dp_dpcd_write()).
 *
 * It returns the bytes transferred, or a negative Linux errno, as
 * i915_drm_dp_dpcd_read().
 */
static i915_dp_ssize_t
i915_drm_dp_dpcd_write(
	struct drm_dp_aux *aux,
	unsigned int offset,
	void *buffer,
	size_t size)
{
	int ret;

	/* Writes the bytes. */
	if (aux->is_remote) {
		ret = drm_dp_mst_dpcd_write(aux, offset, buffer, size);
	} else {
		ret = i915_drm_dp_dpcd_access(aux, DP_AUX_NATIVE_WRITE, offset, buffer, size);
	}

	i915_drm_dp_dump_access(aux, DP_AUX_NATIVE_WRITE, offset, buffer, ret);

	/* Reports a failed write. */
	if (ret < 0)
		return ret;

	/* Succeeded: reports the bytes written. */
	return ret;
}

/*
 * Replaces the receiver capabilities with the extended ones where the sink
 * has them.
 *
 * Before DP 1.3 the DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT bit was reserved;
 * when it is set, DP_DPCD_REV at 0000h may understate the sink, and only a
 * comparison with 2200h tells.  It returns 0, or a negative Linux errno.
 */
static int
i915_drm_dp_read_extended_dpcd_caps(
	struct drm_dp_aux *aux,
	u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 dpcd_ext[DP_RECEIVER_CAP_SIZE];
	int ret;
	int same;

	/* Without the extended field there is nothing to read. */
	if (!(dpcd[DP_TRAINING_AUX_RD_INTERVAL] & DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT))
		return 0;

	/* Reads the extended capabilities. */
	ret = i915_drm_dp_dpcd_read(aux, DP_DP13_DPCD_REV, &dpcd_ext, sizeof(dpcd_ext));
	if (ret < 0)
		return ret;
	if (ret != (int)sizeof(dpcd_ext))
		return -I915_DP_EIO;

	/* An extended revision below the base one is ignored. */
	if (dpcd[DP_DPCD_REV] > dpcd_ext[DP_DPCD_REV]) {
		I915_DP_DRM_DBG_KMS(aux->drm_dev,
				    "%s: Extended DPCD rev less than base DPCD rev (%d > %d)\n",
				    aux->name, dpcd[DP_DPCD_REV], dpcd_ext[DP_DPCD_REV]);
		return 0;
	}

	/* Identical capabilities need no replacing. */
	same = kern_memcmp(dpcd, dpcd_ext, sizeof(dpcd_ext));
	if (same == 0)
		return 0;

	I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: Base DPCD: %*ph\n", aux->name, DP_RECEIVER_CAP_SIZE, dpcd);

	/* Takes the extended capabilities. */
	kern_memcpy(dpcd, dpcd_ext, sizeof(dpcd_ext));

	/* Succeeded: dpcd holds the extended capabilities. */
	return 0;
}

/*
 * Reads the receiver capabilities, and the extended ones where the sink has
 * them (the Linux drm_dp_read_dpcd_caps()).
 *
 * It returns 0, or a negative Linux errno.
 */
static int
i915_drm_dp_read_dpcd_caps(
	struct drm_dp_aux *aux,
	u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int ret;

	/* Reads the base capabilities; a short read or a zero revision is an I/O error. */
	ret = i915_drm_dp_dpcd_read(aux, DP_DPCD_REV, dpcd, DP_RECEIVER_CAP_SIZE);
	if (ret < 0)
		return ret;
	if (ret != DP_RECEIVER_CAP_SIZE || dpcd[DP_DPCD_REV] == 0)
		return -I915_DP_EIO;

	/* Takes the extended capabilities where the sink has them. */
	ret = i915_drm_dp_read_extended_dpcd_caps(aux, dpcd);
	if (ret < 0)
		return ret;

	I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: DPCD: %*ph\n", aux->name, DP_RECEIVER_CAP_SIZE, dpcd);

	/* Succeeded: dpcd holds the capabilities. */
	return ret;
}

/* Reports what the I2C-over-AUX adapter supports. */
static u32
i915_drm_dp_i2c_functionality(
	struct i2c_adapter *adapter)
{
	UNUSED_PARAMETER(adapter);

	/* Plain I2C, emulated SMBus with block reads and calls, and 10-bit addresses. */
	return I2C_FUNC_I2C |
	       I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
	       I2C_FUNC_10BIT_ADDR;
}

/*
 * Switches an I2C write to a write-status update.
 *
 * After an I2C defer or a short I2C acknowledgement of a write, the rest of
 * the message is drained with write-status updates.
 */
static void
i915_drm_dp_i2c_msg_write_status_update(
	struct drm_dp_aux_msg *msg)
{
	/* Only a write changes; the middle-of-transaction bit is kept. */
	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_I2C_WRITE) {
		msg->request &= DP_AUX_I2C_MOT;
		msg->request |= DP_AUX_I2C_WRITE_STATUS_UPDATE;
	}
}

/*
 * Estimates the shortest duration of an AUX request, in microseconds (bit
 * times at 1 MHz).
 */
static int
i915_drm_dp_aux_req_duration(
	const struct drm_dp_aux_msg *msg)
{
	int len;

	/* The fixed parts of a request. */
	len = AUX_PRECHARGE_LEN + AUX_SYNC_LEN + AUX_STOP_LEN + AUX_CMD_LEN + AUX_ADDRESS_LEN + AUX_LENGTH_LEN;

	/* A write carries its data. */
	if ((msg->request & DP_AUX_I2C_READ) == 0)
		len += msg->size * 8;

	/* Succeeded: reports the duration. */
	return len;
}

/*
 * Estimates the shortest duration of an AUX reply, in microseconds.
 *
 * A read's reply carries what was asked; a write's has 0 or 1 data bytes,
 * and 0 is assumed.
 */
static int
i915_drm_dp_aux_reply_duration(
	const struct drm_dp_aux_msg *msg)
{
	int len;

	/* The fixed parts of a reply. */
	len = AUX_PRECHARGE_LEN + AUX_SYNC_LEN + AUX_STOP_LEN + AUX_CMD_LEN + AUX_REPLY_PAD_LEN;

	/* A read's reply carries the data. */
	if (msg->request & DP_AUX_I2C_READ)
		len += msg->size * 8;

	/* Succeeded: reports the duration. */
	return len;
}

/*
 * Estimates the longest duration of the I2C transfer of a message, in
 * microseconds, at a bus speed.
 *
 * Each message is assumed to have its own START, ADDRESS and STOP; the MOT
 * bit and clock stretching are not accounted for.
 */
static int
i915_drm_dp_i2c_msg_duration(
	const struct drm_dp_aux_msg *msg,
	int i2c_speed_khz)
{
	/* The bits of the transfer, at the I2C bit rate. */
	return DIV_ROUND_UP((I2C_START_LEN + I2C_ADDR_LEN + msg->size * I2C_DATA_LEN + I2C_STOP_LEN) * 1000, i2c_speed_khz);
}

/*
 * Reports how many tries a message deserves, from the estimated I2C and AUX
 * durations.
 */
static int
i915_drm_dp_i2c_retry_count(
	const struct drm_dp_aux_msg *msg,
	int i2c_speed_khz)
{
	int aux_time_us;
	int i2c_time_us;

	/* Estimates the AUX round trip and the I2C transfer. */
	aux_time_us = i915_drm_dp_aux_req_duration(msg) + i915_drm_dp_aux_reply_duration(msg);
	i2c_time_us = i915_drm_dp_i2c_msg_duration(msg, i2c_speed_khz);

	/* Succeeded: enough AUX tries to cover the I2C transfer. */
	return DIV_ROUND_UP(i2c_time_us, aux_time_us + AUX_RETRY_INTERVAL);
}

/*
 * Transfers one I2C-over-AUX message, retrying as the replies ask.
 *
 * The transfer hook changes nothing in the message but the reply.  A
 * native or I2C defer waits the retry interval and tries again; the DP
 * specification asks for at least seven tries on AUX_DEFER, and the I2C
 * bus speed is accounted for.  It returns the bytes transferred, or a
 * negative Linux errno.
 */
static int
i915_drm_dp_i2c_do_msg(
	struct drm_dp_aux *aux,
	struct drm_dp_aux_msg *msg)
{
	unsigned int retry;
	unsigned int defer_i2c;
	int ret;
	int max_retries;
	int retry_count;
	int acked;

	/* At least seven tries, more when the I2C transfer is slow. */
	retry_count = i915_drm_dp_i2c_retry_count(msg, I915_DP_AUX_I2C_SPEED_KHZ);
	max_retries = max(7, retry_count);

	/* Tries the message; each I2C defer, up to seven, earns one more try. */
	ret = 0;
	acked = 0;
	defer_i2c = 0;
	for (retry = 0; retry < ((unsigned int)max_retries + defer_i2c); retry++) {
		/* Runs the transaction; a busy channel is simply retried. */
		ret = aux->transfer(aux, msg);
		if (ret < 0) {
			if (ret == -I915_DP_EBUSY)
				continue;

			/* A timeout is normal for a missing device and is not logged as an error. */
			if (ret == -I915_DP_ETIMEDOUT) {
				drm_dbg_kms_ratelimited(aux->drm_dev, "%s: transaction timed out\n",
							aux->name);
			} else {
				I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: transaction failed: %d\n",
						    aux->name, ret);
			}

			return ret;
		}

		/* Checks the native reply; an ACK still needs the I2C reply checked. */
		switch (msg->reply & DP_AUX_NATIVE_REPLY_MASK) {
		case DP_AUX_NATIVE_REPLY_ACK:
			break;

		case DP_AUX_NATIVE_REPLY_NACK:
			I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: native nack (result=%d, size=%zu)\n",
					    aux->name, ret, msg->size);
			return -I915_DP_EREMOTEIO;

		case DP_AUX_NATIVE_REPLY_DEFER:
			/*
			 * Defers long enough to be safe for every use; the I2C bit
			 * rate could be checked, and long legacy cables may force
			 * very low rates.
			 */
			I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: native defer\n", aux->name);
			i915_dp_usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);
			continue;

		default:
			I915_DP_DRM_ERR(aux->drm_dev, "%s: invalid native reply %#04x\n",
					aux->name, msg->reply);
			return -I915_DP_EREMOTEIO;
		}

		/* Checks the I2C reply. */
		switch (msg->reply & DP_AUX_I2C_REPLY_MASK) {
		case DP_AUX_I2C_REPLY_ACK:
			/* Both ACKs: the transfer succeeded; a short write drains the rest. */
			if ((size_t)ret != msg->size)
				i915_drm_dp_i2c_msg_write_status_update(msg);
			acked = 1;
			break;

		case DP_AUX_I2C_REPLY_NACK:
			I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: I2C nack (result=%d, size=%zu)\n",
					    aux->name, ret, msg->size);
			aux->i2c_nack_count++;
			return -I915_DP_EREMOTEIO;

		case DP_AUX_I2C_REPLY_DEFER:
			/* DP Compliance Test 4.2.2.5 needs at least 7 retries of I2C defers. */
			I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: I2C defer\n", aux->name);
			aux->i2c_defer_count++;
			if (defer_i2c < 7)
				defer_i2c++;
			i915_dp_usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);
			i915_drm_dp_i2c_msg_write_status_update(msg);
			continue;

		default:
			I915_DP_DRM_ERR(aux->drm_dev, "%s: invalid I2C reply %#04x\n",
					aux->name, msg->reply);
			return -I915_DP_EREMOTEIO;
		}

		/* Only an acknowledged transfer gets here. */
		break;
	}

	/* Reports a message that ran out of tries. */
	if (!acked) {
		I915_DP_DRM_DBG_KMS(aux->drm_dev, "%s: Too many retries, giving up\n", aux->name);
		return -I915_DP_EREMOTEIO;
	}

	/* Succeeded: reports the bytes transferred. */
	return ret;
}

/* Sets the AUX request of an I2C message: read or write, with MOT unless it stops. */
static void
i915_drm_dp_i2c_msg_set_request(
	struct drm_dp_aux_msg *msg,
	const struct i2c_msg *i2c_msg)
{
	/* A read or a write. */
	if (i2c_msg->flags & I2C_M_RD) {
		msg->request = DP_AUX_I2C_READ;
	} else {
		msg->request = DP_AUX_I2C_WRITE;
	}

	/* The transaction goes on unless this message stops it. */
	if (!(i2c_msg->flags & I2C_M_STOP))
		msg->request |= DP_AUX_I2C_MOT;
}

/*
 * Transfers one I2C-over-AUX chunk until all of it is through.
 *
 * It returns the size a following transfer should use (smaller after a
 * partial reply), or a negative Linux errno (-EPROTO for an empty reply).
 */
static int
i915_drm_dp_i2c_drain_msg(
	struct drm_dp_aux *aux,
	struct drm_dp_aux_msg *orig_msg)
{
	int err;
	int ret;
	struct drm_dp_aux_msg msg;

	/* Works on a copy; the recommended size starts at the whole chunk. */
	ret = orig_msg->size;
	msg = *orig_msg;

	/* Transfers what remains until nothing does. */
	while (msg.size > 0) {
		err = i915_drm_dp_i2c_do_msg(aux, &msg);
		if (err <= 0) {
			if (err == 0)
				return -I915_DP_EPROTO;

			return err;
		}

		/* A partial reply lowers the recommended size. */
		if ((size_t)err < msg.size && err < ret) {
			I915_DP_DRM_DBG_KMS(aux->drm_dev,
					    "%s: Partial I2C reply: requested %zu bytes got %d bytes\n",
					    aux->name, msg.size, err);
			ret = err;
		}

		/* Moves past what was transferred. */
		msg.size -= err;
		msg.buffer = (u8 *)msg.buffer + err;
	}

	/* Succeeded: reports the recommended transfer size. */
	return ret;
}

/*
 * Runs I2C messages over AUX (the I2C adapter algorithm).
 *
 * Each message starts with a bare address packet, is then transferred in
 * chunks as large as possible (smaller after a short reply), and the whole
 * transaction ends with a bare address packet without MOT.  It returns the
 * number of messages, or a negative Linux errno.
 */
static int
i915_drm_dp_i2c_xfer(
	struct i2c_adapter *adapter,
	struct i2c_msg *msgs,
	int num)
{
	struct drm_dp_aux *aux;
	unsigned int i;
	unsigned int j;
	unsigned transfer_size;
	unsigned transfer_limit;
	struct drm_dp_aux_msg msg;
	int err;

	/* Finds the AUX channel the adapter runs over. */
	aux = adapter->algo_data;
	err = 0;

	/* A powered-down sink is not worth a transfer. */
	if (aux->powered_down)
		return -I915_DP_EBUSY;

	/* Limits the chunk size to what one AUX message carries. */
	transfer_limit = I915_VBT_CLAMP(I915_DP_AUX_I2C_TRANSFER_SIZE, 1, DP_AUX_MAX_PAYLOAD_BYTES);

	kern_memset(&msg, 0, sizeof(msg));

	/* Transfers every message. */
	for (i = 0; i < (unsigned int)num; i++) {
		/* Starts the message with a bare address packet. */
		msg.address = msgs[i].addr;
		i915_drm_dp_i2c_msg_set_request(&msg, &msgs[i]);
		msg.buffer = NULL;
		msg.size = 0;
		err = i915_drm_dp_i2c_do_msg(aux, &msg);

		/* Resets the request, which may have become a write-status update. */
		i915_drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

		if (err < 0)
			break;

		/* Transfers the data in chunks as large as possible, smaller after a short reply. */
		transfer_size = transfer_limit;
		for (j = 0; j < (unsigned int)msgs[i].len; j += msg.size) {
			msg.buffer = msgs[i].buf + j;
			msg.size = min(transfer_size, msgs[i].len - j);

			err = i915_drm_dp_i2c_drain_msg(aux, &msg);

			/* Resets the request, which may have become a write-status update. */
			i915_drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

			if (err < 0)
				break;

			transfer_size = err;
		}

		if (err < 0)
			break;
	}

	/* A transfer without errors reports every message. */
	if (err >= 0)
		err = num;

	/* Closes the transaction with a bare address packet without MOT; its result does not matter. */
	msg.request &= ~DP_AUX_I2C_MOT;
	msg.buffer = NULL;
	msg.size = 0;
	(void)i915_drm_dp_i2c_do_msg(aux, &msg);

	/* Reports a failed transfer. */
	if (err < 0)
		return err;

	/* Succeeded: reports the messages transferred. */
	return err;
}
