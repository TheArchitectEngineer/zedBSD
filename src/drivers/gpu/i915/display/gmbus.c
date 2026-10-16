/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GMBUS I2C buses of the hotplug path (see gmbus.h).
 *
 * A bus is an I2C adapter for one DDC pin, made on the first request for
 * the pin.  Its transfer takes the controller lock and the GMBUS power,
 * hands the messages to the controller (xd-gmbus.c), and turns what the
 * controller reports into the Linux errno values the EDID reader of the
 * DP environment compares (-I915_HPD_ENXIO for a refusal,
 * -I915_HPD_ETIMEDOUT, -I915_HPD_EAGAIN), with the same log lines as
 * before.  A transfer whose controller never became ready asks to be
 * retried over GPIO bit-banging, which is not ported: the retry is a
 * recorded step answering -EIO.
 *
 * The bus bookkeeping, the bit-banging fallback and the log lines follow
 * Linux v6.8.12 intel_gmbus.c, which carries this notice:
 *
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2006-2008,2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

#include "hotplug-internal.h"
#include "gmbus.h"
#include "xd-gmbus.h"
#include "xd-io.h"
#include <kern/kcrt.h>

#include <kern/klog.h>
#include <kern/lock.h>

#include <stddef.h>
#include <stdint.h>

/* The force_bit bit a transfer sets when the controller asked to be retried over GPIO. */
#define GMBUS_FORCE_BIT_RETRY (1U << 31)

/* The length of the adapter name prefix "i915 gmbus ". */
#define I915_GMBUS_NAME_PREFIX_LENGTH 11u

/* The most messages one transfer carries (an EDID block read sends three). */
#define I915_GMBUS_MESSAGES_MAX 4

/*
 * What the events of one transfer are logged with: the adapter, whose
 * name the lines carry, and the messages, which the refusal line
 * describes.  It lives on the stack of the transfer.
 */
struct i915_gmbus_transfer_log {
	struct i2c_adapter *adapter;
	struct i2c_msg *msgs;
};

static struct intel_gmbus *i915_to_intel_gmbus(struct i2c_adapter *i2c);
static void i915_gmbus_prepare(struct i915_hpd_world *world);
static uint32_t i915_gmbus_model_read(void *context, uint32_t offset);
static void i915_gmbus_model_write(void *context, uint32_t offset, uint32_t value);
static void i915_gmbus_noted(void *context, unsigned note, unsigned message);
static int i915_do_gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num);
static int i915_gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num);
static int i915_hpd_gmbus_xfer_locked(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num);
static int i915_hpd_bit_xfer_step(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);

/*
 * The algorithm a GMBUS adapter carries: the transfer under the controller
 * lock.
 *
 * It never changes, so every adapter of every world shares it.
 */
static const struct i2c_algorithm i915_hpd_gmbus_algorithm = {
	i915_hpd_gmbus_xfer_locked,
	NULL
};

/*
 * The bit-banging algorithm over GPIO, which is not ported.
 *
 * A transfer that falls back to it is a recorded step answering -EIO.  It
 * never changes and is shared by every adapter.
 */
const struct i2c_algorithm i2c_bit_algo = {
	i915_hpd_bit_xfer_step,
	NULL
};

/*
 * Resets the controller: no pin selected and no interrupt enabled.
 */
void
i915_hpd_intel_gmbus_reset(
	struct drm_i915_private *i915)
{
	struct i915_hpd_world *world;

	/* Leaves the controller idle through the world's window. */
	world = i915_hpd_world_of(i915);
	drv_i915_xd_gmbus_reset(&world->hpd_gmbus_io);
}

/*
 * Counts one more (or one fewer) request for GPIO bit-banging on a bus.
 */
void
i915_hpd_intel_gmbus_force_bit(
	struct i2c_adapter *adapter,
	bool force_bit)
{
	struct intel_gmbus *bus;
	struct drm_i915_private *i915;

	/* Resolves the bus the adapter is embedded in and its device. */
	bus = i915_to_intel_gmbus(adapter);
	i915 = bus->i915;

	/* Moves the request count under the controller lock. */
	mutex_lock(&i915->display.gmbus.mutex);

	/*
	 * A nonzero count makes every transfer on this bus use bit-banging;
	 * the retry bit a timed-out transfer set stays as it is.
	 */
	if (force_bit) {
		bus->force_bit += 1;
	} else {
		bus->force_bit += -1;
	}

	/* Logs the new count. */
	I915_HPD_DRM_DBG_KMS(&i915->drm, "%sabling bit-banging on %s. force bit now %d\n", force_bit ? "en" : "dis", adapter->name, bus->force_bit);

	mutex_unlock(&i915->display.gmbus.mutex);
}

/*
 * Tells whether a bus transfers over GPIO bit-banging instead of GMBUS.
 */
bool
i915_hpd_intel_gmbus_is_forced_bit(
	struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus;

	/* Resolves the bus the adapter is embedded in. */
	bus = i915_to_intel_gmbus(adapter);

	/* A bus with no bit-banging request uses GMBUS. */
	if (bus->force_bit == 0)
		return false;

	/* Succeeded: the bus is forced onto bit-banging. */
	return true;
}

/*
 * Handles the GMBUS interrupt: counts the wake-up of a transfer.
 *
 * The transfer polls the controller's status and never sleeps on the
 * interrupt, so the wake-up is only counted in the world.
 */
void
i915_hpd_intel_gmbus_irq_handler(
	struct drm_i915_private *i915)
{
	/* Counts the wake-up in the device's world. */
	drv_i915_hpd_gmbus_woken(i915_hpd_world_of(i915));
}

/*
 * Returns the GMBUS I2C adapter of a DDC pin, making the bus on first use.
 *
 * Returns NULL for a pin the controller does not have.
 */
struct i2c_adapter *
drv_i915_hpd_gmbus_adapter(
	struct drm_i915_private *i915,
	unsigned int pin)
{
	static const char *const names[GMBUS_NUM_PINS] = {
		NULL, "dpa", "dpb", "dpc", NULL, NULL, NULL, NULL, NULL,
		"tc1", "tc2", "tc3", "tc4", "tc5", "tc6"
	};
	struct i915_hpd_world *world;
	struct intel_gmbus *bus;
	const char *name;
	unsigned position;
	int valid;

	/* Refuses a pin the controller does not have. */
	valid = drv_i915_xd_gmbus_pin_valid(pin);
	if (valid == 0)
		return NULL;

	/* Finds the pin's bus in the world. */
	world = i915_hpd_world_of(i915);
	bus = &world->hpd_gmbus_bus[pin];

	/* A bus already made is handed out as it is. */
	if (bus->i915 != NULL)
		return &bus->adapter;

	/* Prepares the controller lock, window and counters once for every bus of the instance. */
	if (world->hpd_gmbus_mutex_inited == 0) {
		i915_gmbus_prepare(world);
		world->hpd_gmbus_mutex_inited = 1;
	}

	/* Names the adapter "i915 gmbus <pin name>". */
	kern_memset(bus, 0, sizeof(*bus));
	kern_memcpy(bus->adapter.name, "i915 gmbus ", I915_GMBUS_NAME_PREFIX_LENGTH);
	name = names[pin];
	position = I915_GMBUS_NAME_PREFIX_LENGTH;
	while (*name != '\0' && position < sizeof(bus->adapter.name) - 1u) {
		bus->adapter.name[position] = *name;
		name++;
		position++;
	}
	bus->adapter.name[position] = '\0';

	/* Binds the locked transfer, one retry, and the pin.  A nonzero i915 marks the bus as made. */
	bus->adapter.algo = &i915_hpd_gmbus_algorithm;
	bus->adapter.retries = 1;
	bus->pin = pin;
	bus->i915 = i915;

	/* Leaves the controller idle before the bus is used. */
	i915_hpd_intel_gmbus_reset(i915);

	/* Succeeded: the pin's bus is made. */
	return &bus->adapter;
}

/*
 * Forgets every bus of the world, so a new hotplug path makes them again.
 */
void
drv_i915_hpd_gmbus_forget(
	struct i915_hpd_world *world)
{
	/* Clears the buses and marks the controller lock, window and counters as not prepared. */
	kern_memset(world->hpd_gmbus_bus, 0, sizeof(world->hpd_gmbus_bus));
	world->hpd_gmbus_mutex_inited = 0;
}

/* Returns the bus an adapter is embedded in. */
static struct intel_gmbus *
i915_to_intel_gmbus(
	struct i2c_adapter *i2c)
{
	/* The adapter is the first member of the bus. */
	return container_of(i2c, struct intel_gmbus, adapter);
}

/*
 * Prepares the controller of the running instance: its lock, the window
 * on the instance's registers (the MMIO, or a model test's registers
 * through the world), and its counters.
 */
static void
i915_gmbus_prepare(
	struct i915_hpd_world *world)
{
	struct i915_xd_io_hooks hooks;

	/* Prepares the lock every transfer of every bus holds. */
	(void)mutex_init(&world->hpd_i915.display.gmbus.mutex, LOCK_RANK_DEVICE, "i915-gmbus");

	/* Opens the window on the instance's MMIO. */
	drv_i915_xd_io_init(&world->hpd_gmbus_io, world->hpd.m, NULL);

	/* A model test's registers are reached through the world; its time is the kernel's. */
	if (world->hpd.fake != NULL) {
		hooks.read = i915_gmbus_model_read;
		hooks.write = i915_gmbus_model_write;
		hooks.wait = NULL;
		hooks.lock = NULL;
		hooks.context = world;
		drv_i915_xd_io_set_hooks(&world->hpd_gmbus_io, &hooks);
	}

	/* Starts the controller's counters. */
	drv_i915_xd_gmbus_init(&world->hpd_gmbus);
}

/* Reads a register of a model test's instance through the world. */
static uint32_t
i915_gmbus_model_read(
	void *context,
	uint32_t offset)
{
	uint32_t value;

	/* The world routes the read to the model. */
	value = drv_i915_hpd_read(context, offset);

	/* Succeeded: reports the register's value. */
	return value;
}

/* Writes a register of a model test's instance through the world. */
static void
i915_gmbus_model_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	/* The world routes the write to the model. */
	(void)drv_i915_hpd_write(context, offset, value);
}

/* Logs an event of a transfer with the lines the hotplug path has always printed. */
static void
i915_gmbus_noted(
	void *context,
	unsigned note,
	unsigned message)
{
	struct i915_gmbus_transfer_log *log;
	struct i2c_msg *msg;
	char direction;

	/* Finds the adapter and the message the event is about. */
	log = context;
	msg = &log->msgs[message];

	/* Logs the event. */
	switch (note) {
	case I915_XD_GMBUS_NOTE_STAYED_ACTIVE:
		I915_HPD_DRM_DBG_KMS(NULL, "GMBUS [%s] timed out after NAK\n", log->adapter->name);
		break;
	case I915_XD_GMBUS_NOTE_REFUSED:
		direction = 'w';
		if ((msg->flags & I2C_M_RD) != 0)
			direction = 'r';
		I915_HPD_DRM_DBG_KMS(NULL, "GMBUS [%s] NAK for addr: %04x %c(%d)\n", log->adapter->name, msg->addr, direction, msg->len);
		break;
	case I915_XD_GMBUS_NOTE_RETRY:
		I915_HPD_DRM_DBG_KMS(NULL, "GMBUS [%s] NAK on first message, retry\n", log->adapter->name);
		break;
	default:
		break;
	}
}

/*
 * Transfers the messages over the controller.
 *
 * Answers the number of messages transferred, or a Linux errno:
 * -I915_HPD_ENXIO when a target refused, -I915_HPD_ETIMEDOUT for a bus
 * that did not go idle, -I915_HPD_EAGAIN to have the transfer retried
 * over bit-banging, -I915_HPD_EIO for a transfer of more messages than
 * the hotplug path ever sends.
 */
static int
i915_do_gmbus_xfer(
	struct i2c_adapter *adapter,
	struct i2c_msg *msgs,
	int num)
{
	struct i915_xd_gmbus_message messages[I915_GMBUS_MESSAGES_MAX];
	struct i915_xd_gmbus_request request;
	struct i915_xd_gmbus_result result;
	struct i915_gmbus_transfer_log log;
	struct i915_hpd_world *world;
	struct intel_gmbus *bus;
	int index;
	bool irqs;

	/* Resolves the bus and its world. */
	bus = i915_to_intel_gmbus(adapter);
	world = i915_hpd_world_of(bus->i915);

	/* Refuses more messages than the request holds. */
	if (num < 0 || num > I915_GMBUS_MESSAGES_MAX) {
		I915_HPD_DRM_DBG_KMS(NULL, "GMBUS [%s] %d messages are more than a transfer carries\n", adapter->name, num);
		return -I915_HPD_EIO;
	}

	/* Copies the messages into the controller's form. */
	for (index = 0; index < num; index++) {
		messages[index].address = msgs[index].addr;
		messages[index].read = 0;
		if ((msgs[index].flags & I2C_M_RD) != 0)
			messages[index].read = 1;
		messages[index].data = msgs[index].buf;
		messages[index].length = msgs[index].len;
	}

	/* Learns whether the controller may raise its interrupts: only while the device takes them. */
	irqs = i915_hpd_intel_irqs_enabled(bus->i915);

	/* Asks for the transfer on the bus's pin; its events are logged as they happen. */
	log.adapter = adapter;
	log.msgs = msgs;
	request.pin = bus->pin;
	request.messages = messages;
	request.count = (unsigned)num;
	request.interrupts = 0;
	if (irqs)
		request.interrupts = 1;
	request.noted = i915_gmbus_noted;
	request.context = &log;
	drv_i915_xd_gmbus_transfer(&world->hpd_gmbus, &world->hpd_gmbus_io, &request, &result);

	/* Turns a failed outcome into the answer the I2C caller expects. */
	switch (result.outcome) {
	case I915_XD_GMBUS_DONE:
		break;
	case I915_XD_GMBUS_LINGERED:
		I915_HPD_DRM_DBG_KMS(NULL, "GMBUS [%s] timed out waiting for idle\n", adapter->name);
		return -I915_HPD_ETIMEDOUT;
	case I915_XD_GMBUS_NOT_READY:
		/*
		 * The controller may not reach these pins: the I2C caller
		 * retries, and the retry goes over bit-banging.
		 */
		I915_HPD_DRM_DBG_KMS(NULL, "GMBUS [%s] timed out, falling back to bit banging on pin %d\n", adapter->name, bus->pin);
		return -I915_HPD_EAGAIN;
	case I915_XD_GMBUS_HUNG:
		return -I915_HPD_ETIMEDOUT;
	default:
		return -I915_HPD_ENXIO;
	}

	/* Succeeded: reports the number of messages transferred. */
	return num;
}

/*
 * Transfers the messages with a GMBUS power reference, over GMBUS or, when
 * forced, over bit-banging.
 */
static int
i915_gmbus_xfer(
	struct i2c_adapter *adapter,
	struct i2c_msg *msgs,
	int num)
{
	struct intel_gmbus *bus;
	struct drm_i915_private *i915;
	intel_wakeref_t wakeref;
	int ret;

	/* Resolves the bus and its device. */
	bus = i915_to_intel_gmbus(adapter);
	i915 = bus->i915;

	/* Holds the GMBUS power domain across the transfer. */
	wakeref = drv_i915_hpd_intel_display_power_get(i915, POWER_DOMAIN_GMBUS);

	/*
	 * A forced bus uses bit-banging, and a failure there clears the
	 * retry request; a GMBUS transfer that asks to be retried sets it.
	 */
	if (bus->force_bit != 0) {
		ret = i2c_bit_algo.master_xfer(adapter, msgs, num);
		if (ret < 0)
			bus->force_bit &= ~GMBUS_FORCE_BIT_RETRY;
	} else {
		ret = i915_do_gmbus_xfer(adapter, msgs, num);
		if (ret == -I915_HPD_EAGAIN)
			bus->force_bit |= GMBUS_FORCE_BIT_RETRY;
	}

	/* Drops the power reference. */
	drv_i915_hpd_intel_display_power_put(i915, POWER_DOMAIN_GMBUS, wakeref);

	/* Reports a failed transfer. */
	if (ret < 0)
		return ret;

	/* Succeeded: reports the number of messages transferred. */
	return ret;
}

/*
 * Transfers under the controller lock.
 *
 * The single GMBUS controller is shared by every pin; the lock also keeps
 * the hotplug work's detection and a test thread's read apart.
 */
static int
i915_hpd_gmbus_xfer_locked(
	struct i2c_adapter *adapter,
	struct i2c_msg *msgs,
	int num)
{
	struct intel_gmbus *bus;
	int ret;

	/* Resolves the bus whose device holds the controller lock. */
	bus = i915_to_intel_gmbus(adapter);

	/* Runs the transfer alone on the controller. */
	mutex_lock(&bus->i915->display.gmbus.mutex);

	ret = i915_gmbus_xfer(adapter, msgs, num);

	mutex_unlock(&bus->i915->display.gmbus.mutex);

	/* Reports a failed transfer. */
	if (ret < 0)
		return ret;

	/* Succeeded: reports the number of messages transferred. */
	return ret;
}

/* Records a transfer that fell back to the unported GPIO bit-banging. */
static int
i915_hpd_bit_xfer_step(
	struct i2c_adapter *adap,
	struct i2c_msg *msgs,
	int num)
{
	UNUSED_PARAMETER(msgs);
	UNUSED_PARAMETER(num);

	/* Names the step and answers the I/O error the fallback stands for. */
	kern_logf("i915: hpd step i2c_bit_algo.master_xfer (GPIO bit-banging not ported): %s -> -EIO\n", adap->name);
	return -I915_HPD_EIO;
}
