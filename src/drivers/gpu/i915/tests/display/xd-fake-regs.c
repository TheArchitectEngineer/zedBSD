/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register model of the xd host tests (see xd-fake-regs.h).
 *
 * The registers are kept in a small table searched in order; a register
 * nobody wrote reads 0.  Time only passes when the code under test waits
 * through the window's wait hook, so a test fully decides when a state
 * bit appears.
 */

#include "xd-fake-regs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* How many registers, rules, later changes and held locks the model keeps. */
#define I915_XD_FAKE_REGISTERS		2048U
#define I915_XD_FAKE_RULES		128U
#define I915_XD_FAKE_EVENTS		128U
#define I915_XD_FAKE_LOCKS		16U

/* One register of the model and its value. */
struct i915_xd_fake_register {
	uint32_t offset;
	uint32_t value;
};

/*
 * How the model answers a write of one register: by mirroring bits into
 * a target register after a delay, or by calling the test's function.
 */
struct i915_xd_fake_rule {
	/* The register whose write triggers the rule. */
	uint32_t offset;

	/* The mirror: the bits taken from the written value, where they go, how far they move, and when. */
	uint32_t source_mask;
	uint32_t target;
	int shift;
	unsigned delay_us;

	/* The test's own answer; NULL for a mirror. */
	i915_xd_fake_write_fn function;
	void *context;
};

/* A change of a register the model makes when its time comes. */
struct i915_xd_fake_event {
	/* The time (model microseconds) the change happens. */
	uint64_t due;

	/* The register, and the bits cleared and set in it. */
	uint32_t offset;
	uint32_t clear;
	uint32_t set;
};

/*
 * The model's state, one per test process, emptied by i915_xd_fake_reset().
 *
 * The registers, the rules and the pending changes are each a used
 * prefix of their table; the clock only moves forward.
 */
static struct {
	struct i915_xd_fake_register registers[I915_XD_FAKE_REGISTERS];
	unsigned register_count;
	struct i915_xd_fake_rule rules[I915_XD_FAKE_RULES];
	unsigned rule_count;
	struct i915_xd_fake_event events[I915_XD_FAKE_EVENTS];
	unsigned event_count;
	uint64_t now;
	unsigned reads;
	unsigned writes;
	unsigned held[I915_XD_FAKE_LOCKS];
	unsigned held_count;
	unsigned lock_violations;
} i915_xd_fake;

static uint32_t i915_xd_fake_read_hook(void *context, uint32_t offset);
static void i915_xd_fake_write_hook(void *context, uint32_t offset, uint32_t value);
static void i915_xd_fake_wait_hook(void *context, unsigned microseconds);
static void i915_xd_fake_lock_hook(void *context, unsigned lock, int taken);
static struct i915_xd_fake_register *i915_xd_fake_find(uint32_t offset, int create);
static void i915_xd_fake_run_events(void);
static void i915_xd_fake_apply_rules(uint32_t offset, uint32_t value);
static uint32_t i915_xd_fake_move(uint32_t bits, int shift);

/*
 * Empties the model: no registers, no rules, no pending changes, the
 * clock at 0 and no locks held.
 */
void
i915_xd_fake_reset(void)
{
	/* Forgets every register, rule and pending change. */
	i915_xd_fake.register_count = 0U;
	i915_xd_fake.rule_count = 0U;
	i915_xd_fake.event_count = 0U;

	/* Restarts the clock and the counters. */
	i915_xd_fake.now = 0U;
	i915_xd_fake.reads = 0U;
	i915_xd_fake.writes = 0U;

	/* Starts with no locks held and no broken order seen. */
	i915_xd_fake.held_count = 0U;
	i915_xd_fake.lock_violations = 0U;
}

/*
 * Fills the hooks a struct i915_xd_io takes so that it reaches the model.
 */
void
i915_xd_fake_hooks(
	struct i915_xd_io_hooks *hooks)
{
	/* Points every hook at the model. */
	hooks->read = i915_xd_fake_read_hook;
	hooks->write = i915_xd_fake_write_hook;
	hooks->wait = i915_xd_fake_wait_hook;
	hooks->lock = i915_xd_fake_lock_hook;
	hooks->context = NULL;
}

/*
 * Sets a register of the model directly, as the hardware or the
 * firmware would have left it; no rule runs.
 */
void
i915_xd_fake_set(
	uint32_t offset,
	uint32_t value)
{
	struct i915_xd_fake_register *entry;

	/* Finds or adds the register and stores the value. */
	entry = i915_xd_fake_find(offset, 1);
	entry->value = value;
}

/*
 * Reads a register of the model without counting the read.
 */
uint32_t
i915_xd_fake_get(
	uint32_t offset)
{
	struct i915_xd_fake_register *entry;

	/* A register nobody wrote reads 0. */
	entry = i915_xd_fake_find(offset, 0);
	if (entry == NULL)
		return 0U;

	/* Succeeded: reports the register's value. */
	return entry->value;
}

/*
 * Reports the model's clock in microseconds.
 */
uint64_t
i915_xd_fake_now(void)
{
	/* Reports the time that has passed through the wait hook. */
	return i915_xd_fake.now;
}

/*
 * Schedules a change of a register: after delay_us of model time the
 * bits clear are cleared and the bits set are set.
 */
void
i915_xd_fake_later(
	unsigned delay_us,
	uint32_t offset,
	uint32_t clear,
	uint32_t set)
{
	struct i915_xd_fake_event *event;

	/* A change that is due now happens at once. */
	if (delay_us == 0U) {
		i915_xd_fake_set(offset, (i915_xd_fake_get(offset) & ~clear) | set);
		return;
	}

	/* A full table is a mistake in the test. */
	if (i915_xd_fake.event_count == I915_XD_FAKE_EVENTS) {
		fprintf(stderr, "xd-fake-regs: too many pending changes\n");
		abort();
	}

	/* Adds the change to the pending ones. */
	event = &i915_xd_fake.events[i915_xd_fake.event_count];
	event->due = i915_xd_fake.now + delay_us;
	event->offset = offset;
	event->clear = clear;
	event->set = set;
	i915_xd_fake.event_count++;
}

/*
 * Adds a rule: when the register at offset is written, the written bits
 * under source_mask appear in the register target, moved by shift
 * (left when positive, right when negative), delay_us later.  The bits
 * the mask covers in the target are replaced, so a cleared request bit
 * clears the state bit.
 */
void
i915_xd_fake_rule_mirror(
	uint32_t offset,
	uint32_t source_mask,
	uint32_t target,
	int shift,
	unsigned delay_us)
{
	struct i915_xd_fake_rule *rule;

	/* A full table is a mistake in the test. */
	if (i915_xd_fake.rule_count == I915_XD_FAKE_RULES) {
		fprintf(stderr, "xd-fake-regs: too many rules\n");
		abort();
	}

	/* Adds the mirror. */
	rule = &i915_xd_fake.rules[i915_xd_fake.rule_count];
	rule->offset = offset;
	rule->source_mask = source_mask;
	rule->target = target;
	rule->shift = shift;
	rule->delay_us = delay_us;
	rule->function = NULL;
	rule->context = NULL;
	i915_xd_fake.rule_count++;
}

/*
 * Adds a rule that calls the test's function after every write of the
 * register at offset; the written value is already stored.
 */
void
i915_xd_fake_rule_call(
	uint32_t offset,
	i915_xd_fake_write_fn function,
	void *context)
{
	struct i915_xd_fake_rule *rule;

	/* A full table is a mistake in the test. */
	if (i915_xd_fake.rule_count == I915_XD_FAKE_RULES) {
		fprintf(stderr, "xd-fake-regs: too many rules\n");
		abort();
	}

	/* Adds the call. */
	rule = &i915_xd_fake.rules[i915_xd_fake.rule_count];
	rule->offset = offset;
	rule->source_mask = 0U;
	rule->target = 0U;
	rule->shift = 0;
	rule->delay_us = 0U;
	rule->function = function;
	rule->context = context;
	i915_xd_fake.rule_count++;
}

/*
 * Reports how many register reads went through the hooks.
 */
unsigned
i915_xd_fake_reads(void)
{
	/* Reports the count since the last reset. */
	return i915_xd_fake.reads;
}

/*
 * Reports how many register writes went through the hooks.
 */
unsigned
i915_xd_fake_writes(void)
{
	/* Reports the count since the last reset. */
	return i915_xd_fake.writes;
}

/*
 * Reports how many times a lock was taken while a lock that must come
 * after it in the order was held.
 */
unsigned
i915_xd_fake_lock_violations(void)
{
	/* Reports the count since the last reset. */
	return i915_xd_fake.lock_violations;
}

/*
 * Reports how many xd locks are held now.
 */
unsigned
i915_xd_fake_locks_held(void)
{
	/* Reports the depth of the held locks. */
	return i915_xd_fake.held_count;
}

/* Answers a read of the window from the model. */
static uint32_t
i915_xd_fake_read_hook(
	void *context,
	uint32_t offset)
{
	uint32_t value;

	(void)context;

	/* Brings in every change that is due, then reads. */
	i915_xd_fake_run_events();
	i915_xd_fake.reads++;
	value = i915_xd_fake_get(offset);

	/* Succeeded: reports the register's value. */
	return value;
}

/* Takes a write of the window into the model and runs the register's rules. */
static void
i915_xd_fake_write_hook(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	(void)context;

	/* Stores the value, then lets the hardware answer it. */
	i915_xd_fake.writes++;
	i915_xd_fake_set(offset, value);
	i915_xd_fake_apply_rules(offset, value);
}

/* Lets model time pass and brings in the changes that became due. */
static void
i915_xd_fake_wait_hook(
	void *context,
	unsigned microseconds)
{
	(void)context;

	/* Moves the clock and applies what is due. */
	i915_xd_fake.now += microseconds;
	i915_xd_fake_run_events();
}

/* Records a lock taken or released and counts a taking out of order. */
static void
i915_xd_fake_lock_hook(
	void *context,
	unsigned lock,
	int taken)
{
	unsigned index;

	(void)context;

	/* A release removes the newest holding of that lock. */
	if (taken == 0) {
		for (index = i915_xd_fake.held_count; index > 0U; index--) {
			if (i915_xd_fake.held[index - 1U] == lock) {
				i915_xd_fake.held[index - 1U] = i915_xd_fake.held[i915_xd_fake.held_count - 1U];
				i915_xd_fake.held_count--;
				return;
			}
		}

		/* Releasing a lock that is not held breaks the order too. */
		i915_xd_fake.lock_violations++;
		return;
	}

	/* A taking is out of order when a lock of the same or a later rank is held. */
	for (index = 0U; index < i915_xd_fake.held_count; index++) {
		if (i915_xd_fake.held[index] >= lock)
			i915_xd_fake.lock_violations++;
	}

	/* A full stack is a mistake in the test. */
	if (i915_xd_fake.held_count == I915_XD_FAKE_LOCKS) {
		fprintf(stderr, "xd-fake-regs: too many locks held\n");
		abort();
	}

	/* Records the lock as held. */
	i915_xd_fake.held[i915_xd_fake.held_count] = lock;
	i915_xd_fake.held_count++;
}

/* Finds a register of the model, adding it when create is nonzero. */
static struct i915_xd_fake_register *
i915_xd_fake_find(
	uint32_t offset,
	int create)
{
	struct i915_xd_fake_register *entry;
	unsigned index;

	/* Looks for the register among those the model has. */
	for (index = 0U; index < i915_xd_fake.register_count; index++) {
		if (i915_xd_fake.registers[index].offset == offset)
			return &i915_xd_fake.registers[index];
	}

	/* A register that is only read is not added. */
	if (create == 0)
		return NULL;

	/* A full table is a mistake in the test. */
	if (i915_xd_fake.register_count == I915_XD_FAKE_REGISTERS) {
		fprintf(stderr, "xd-fake-regs: too many registers\n");
		abort();
	}

	/* Adds the register with the value 0. */
	entry = &i915_xd_fake.registers[i915_xd_fake.register_count];
	entry->offset = offset;
	entry->value = 0U;
	i915_xd_fake.register_count++;

	/* Succeeded: reports the new register. */
	return entry;
}

/* Applies, in the order they were scheduled, the changes whose time has come. */
static void
i915_xd_fake_run_events(void)
{
	struct i915_xd_fake_event *event;
	uint32_t value;
	unsigned index;
	unsigned kept;

	/* Applies the due changes and keeps the others, in order. */
	kept = 0U;
	for (index = 0U; index < i915_xd_fake.event_count; index++) {
		event = &i915_xd_fake.events[index];

		/* A change still in the future stays pending. */
		if (event->due > i915_xd_fake.now) {
			i915_xd_fake.events[kept] = *event;
			kept++;
			continue;
		}

		/* Makes the change. */
		value = i915_xd_fake_get(event->offset);
		value = (value & ~event->clear) | event->set;
		i915_xd_fake_set(event->offset, value);
	}

	i915_xd_fake.event_count = kept;
}

/* Runs the rules of a register just written. */
static void
i915_xd_fake_apply_rules(
	uint32_t offset,
	uint32_t value)
{
	struct i915_xd_fake_rule *rule;
	uint32_t covered;
	uint32_t moved;
	unsigned index;

	/* Runs every rule of the register, in the order they were added. */
	for (index = 0U; index < i915_xd_fake.rule_count; index++) {
		rule = &i915_xd_fake.rules[index];

		/* A rule of another register does not answer. */
		if (rule->offset != offset)
			continue;

		/* A test's function answers by itself. */
		if (rule->function != NULL) {
			rule->function(offset, value, rule->context);
			continue;
		}

		/* A mirror replaces the bits it covers in the target. */
		covered = i915_xd_fake_move(rule->source_mask, rule->shift);
		moved = i915_xd_fake_move(value & rule->source_mask, rule->shift);
		i915_xd_fake_later(rule->delay_us, rule->target, covered, moved);
	}
}

/* Moves bits left (positive shift) or right (negative shift). */
static uint32_t
i915_xd_fake_move(
	uint32_t bits,
	int shift)
{
	/* A negative shift moves the bits towards bit 0. */
	if (shift < 0)
		return bits >> (unsigned)(-shift);

	/* Succeeded: the bits moved up. */
	return bits << (unsigned)shift;
}
