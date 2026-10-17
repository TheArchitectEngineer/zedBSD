/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The vertical blank helpers of the one-screen path (see vblank.h): the
 * nesting of the update section's interrupts on the device's backend,
 * and the backend hooks of a panel run that take a vertical blank
 * reference, sleep until the pipe's next blank and complete the flip
 * event.
 */

#include "modeset-internal.h"
#include "takeover-internal.h"
#include "vblank.h"
#include "pipe.h"
#include "diagnostics.h"
#include "interrupts.h"
#include "modeset.h"
#include "../mmio.h"
#include "xd-pipe.h"

#include <kern/irq.h>

#include <uapi/errno.h>

static uint32_t i915_lcd_kernel_frame(void *ctx);

/*
 * Turns interrupts off for the update section (the Linux local_irq_disable()).
 *
 * The section state is kept in the world, so a save inside the section
 * finds interrupts already off and its restore leaves them off.
 */
void
drv_i915_lcd_irq_disable(
	struct i915_lcd_world *world,
	struct drm_i915_private *i915)
{
	/* Turns interrupts off through the backend unless the section already did. */
	if (!world->i915_lcd_irqs_off)
		i915->emit->irq_off(i915->emit->ctx);

	/* From here the section has interrupts off. */
	world->i915_lcd_irqs_off = 1;
}

/*
 * Turns interrupts back on after the update section (the Linux
 * local_irq_enable()).
 */
void
drv_i915_lcd_irq_enable(
	struct i915_lcd_world *world,
	struct drm_i915_private *i915)
{
	/* Turns interrupts on through the backend when the section had them off. */
	if (world->i915_lcd_irqs_off)
		i915->emit->irq_on(i915->emit->ctx);

	/* From here the section has interrupts on. */
	world->i915_lcd_irqs_off = 0;
}

/*
 * Takes a vblank reference on a pipe for a panel run (the backend's
 * drm_crtc_vblank_get()).
 *
 * 0, or the Linux -EINVAL the hook contract names when the pipe's
 * interrupts are not enabled.
 */
int
drv_i915_lcd_kernel_vblank_get(
	void *ctx,
	int pipe)
{
	struct i915_lcd_kernel *k;
	int error;

	/* The context is the panel run. */
	k = ctx;

	/* Takes the reference on the display interrupt state. */
	error = drv_i915_drm_vblank_get(k->d->irq, (unsigned)pipe);
	if (error != 0)
		return -I915_LCD_EINVAL;

	/* Succeeded: the pipe's vblank interrupt is on and referenced. */
	return 0;
}

/*
 * Gives a vblank reference of a panel run back (the backend's
 * drm_crtc_vblank_put()).
 */
void
drv_i915_lcd_kernel_vblank_put(
	void *ctx,
	int pipe)
{
	struct i915_lcd_kernel *k;

	/* The context is the panel run. */
	k = ctx;

	/* The last put disables the pipe's vblank interrupt at once. */
	drv_i915_drm_vblank_put(k->d->irq, (unsigned)pipe);
}

/*
 * Sleeps until the pipe's next vblank interrupt or the timeout (the
 * backend's schedule_timeout() on the pipe's vblank wait queue).
 *
 * The ticks left (at least 1) when the vblank came, 0 when it did not.
 */
long
drv_i915_lcd_kernel_vblank_sleep(
	void *ctx,
	int pipe,
	long ticks)
{
	struct i915_lcd_kernel *k;
	bool on;
	int error;

	/* The context is the panel run. */
	k = ctx;

	/*
	 * The Linux text enables local interrupts before schedule_timeout():
	 * reads the CPU's real state and puts it back, counting a sleep
	 * entered with interrupts off.
	 */
	on = kern_irq_disable();
	if (on) {
		kern_irq_enable();
	} else {
		k->sleep_irq_off++;
	}

	/* Waits for one new vblank of the pipe, at most the display ticks asked for (10 ms each, not kernel ticks). */
	error = drv_i915_wait_vblank(k->d->irq, (unsigned)pipe, 1u, (unsigned)ticks * 10u, i915_lcd_kernel_frame, k, 0);
	k->vblank_sleeps++;

	/* A broken time base is the run's anomaly, not a timeout. */
	if (error == EIO)
		drv_i915_lcd_backend_fault("time base fault while waiting for a vblank\n");

	/* Reports a sleep that ended without the vblank. */
	if (error != 0)
		return 0;

	/* A sleep that ended on the vblank leaves at least one tick. */
	if (ticks > 1)
		return ticks - 1;

	/* Succeeded: the vblank came in the last tick. */
	return 1;
}

/*
 * Arms the flip event of a panel run (the backend's
 * drm_crtc_arm_vblank_event()).
 *
 * The event completes at the pipe's next vblank after now; the vblank
 * reference was taken by the Linux text.
 */
void
drv_i915_lcd_kernel_arm_event(
	void *ctx,
	int pipe)
{
	struct i915_lcd_kernel *k;

	/* The context is the panel run. */
	k = ctx;

	/* Records the armed event and the frame it was armed in. */
	k->event_armed = 1;
	k->event_pipe = pipe;
	k->event_frame = i915_lcd_kernel_frame(k);
}

/*
 * Waits for the armed flip event of a panel run.
 *
 * The event completes with a new vblank interrupt of the pipe after the
 * wait began and the frame counter past the arming frame.  0; the Linux
 * -EINVAL when no event of the pipe is armed, -ETIMEDOUT when it did not
 * complete within timeout_ms, -EIO when the time base failed.
 */
int
drv_i915_lcd_kernel_wait_event(
	void *ctx,
	int pipe,
	unsigned timeout_ms)
{
	struct i915_lcd_kernel *k;
	uint32_t frame;
	int error;

	/* The context is the panel run. */
	k = ctx;

	/* Refuses a wait for an event that is not armed on this pipe. */
	if (!k->event_armed)
		return -I915_LCD_EINVAL;
	if (pipe != k->event_pipe)
		return -I915_LCD_EINVAL;

	/* Waits for a new vblank interrupt of the pipe. */
	error = drv_i915_wait_vblank(k->d->irq, (unsigned)pipe, 1u, timeout_ms, i915_lcd_kernel_frame, k, 0);

	/* A vblank that left the frame counter at the arming frame did not complete the event. */
	if (error == 0) {
		frame = i915_lcd_kernel_frame(k);
		if (frame == k->event_frame)
			error = ETIMEDOUT;
	}

	/* Reports a broken time base. */
	if (error == EIO)
		return I915_LCD_EIO;

	/* Reports an event that did not complete in time. */
	if (error != 0)
		return I915_LCD_ETIMEDOUT;

	/* The event is complete: it is no longer armed. */
	k->event_armed = 0;

	/* Succeeded: the flip event completed. */
	return 0;
}

/*
 * Forgets the armed flip event of a panel run (the backend's
 * drm_crtc_vblank_off() on a pending event).
 *
 * A later wait is refused and a late vblank completes nothing.  The event
 * record is the run's own; the interrupt handler only advances the pipe's
 * counters under the interrupt lock.
 */
void
drv_i915_lcd_kernel_cancel_event(
	void *ctx,
	int pipe)
{
	struct i915_lcd_kernel *k;

	UNUSED_PARAMETER(pipe);

	/* The context is the panel run. */
	k = ctx;

	/* Counts an event that was still armed. */
	if (k->event_armed)
		k->events_cancelled++;

	/* The event is forgotten. */
	k->event_armed = 0;
}

/*
 * Reads the frame counter a panel run compares against (the frame hook of
 * the run's vblank waits).
 *
 * The counter is the one of the run's pipe: the pipe its parameters name
 * (pipe B for the HDMI display), or the panel's pipe A when the run has
 * none.  Reading pipe A's counter for a run on pipe B saw it
 * stand at 0, so every flip event of the HDMI display timed out and the
 * resident run failed at its stop.
 */
static uint32_t
i915_lcd_kernel_frame(
	void *ctx)
{
	struct i915_lcd_kernel *k;
	uint32_t reg;
	uint32_t frame;
	int pipe;

	/* The context is the panel run. */
	k = ctx;

	/* The pipe the run drives: its parameters', or the panel's pipe A. */
	pipe = 0;
	if (k->p != NULL)
		pipe = k->p->pipe;

	/* Reads the pipe's frame counter. */
	reg = i915_mmio_reg_offset(PIPE_FRMCOUNT_G4X(pipe));
	frame = drv_i915_read32(k->d->mmio, reg);

	/* Succeeded: reports the frame number. */
	return frame;
}
