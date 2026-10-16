/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Stage A4, the static tier: the system-management unit's mailbox, a fan
 * at a fixed duty, the temperature, and the driver's watch over both.
 *
 * Stage A3 had the security processor start the system-management unit's
 * firmware (PMFW).  This stage checks that it runs and asks its versions,
 * but enables none of its features: the fan controller of the thermal
 * block is set to a static duty (50 % by default), the thermal block's
 * alerts are sent to the interrupt ring (high at the board's shutdown
 * temperature), and a thread reads the temperature and the fan's speed
 * once a second.  Past the limit (95 degrees Celsius by default), on a
 * high alert or on the critical-temperature trip, the driver faults the
 * GPU: it marks the fault and the GPU takes no more work; the system keeps
 * running.  The firmware's own trip protects the GPU at the hardware
 * limit.
 *
 * The stage also checks that the interrupt ring delivers: it raises the
 * low alert's threshold above the temperature for a moment, which makes
 * the thermal block send one low alert.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <kern/waitq.h>
#include <uapi/amdrdna.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-private.h"
#include "drivers/gpu/amdrdna/amdrdna-regs.h"

/*
 * The messages the stage sends (the unit's firmware version, the
 * driver-interface version it implements, and the preparation for its
 * unload), the interface version this driver was written against, and
 * the mailbox's answers (OK, failed, unknown message, a prerequisite
 * missing, busy).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v11_0_ppsmc.h
 * (PPSMC_MSG_GetSmuVersion, PPSMC_MSG_GetDriverIfVersion,
 * PPSMC_MSG_PrepareMp1ForUnload, PPSMC_Result_OK, PPSMC_Result_Failed,
 * PPSMC_Result_UnknownCmd, PPSMC_Result_CmdRejectedPrereq,
 * PPSMC_Result_CmdRejectedBusy) and
 * drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0.h
 * (SMU11_DRIVER_IF_VERSION_NV10).
 */
#define SMU_MESSAGE_VERSION		0x02U
#define SMU_MESSAGE_INTERFACE		0x03U
#define SMU_MESSAGE_UNLOAD		0x32U
#define SMU_INTERFACE_EXPECTED		0x37U
#define SMU_ANSWER_OK			0x01U
#define SMU_ANSWER_FAILED		0xffU
#define SMU_ANSWER_UNKNOWN		0xfeU
#define SMU_ANSWER_PREREQUISITE		0xfdU
#define SMU_ANSWER_BUSY			0xfcU

/*
 * The waits: an answer polled each millisecond for 2 s (Linux polls each
 * microsecond for 20 times its 100 ms timeout), the firmware's running bit
 * for 1 s, the fan's first turns for 5 s, the low alert of the interrupt
 * check for 500 ms, and the watch's period of 1 s.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu_cmn.c (__smu_cmn_poll_stat).
 */
#define SMU_ANSWER_POLLS		2000U
#define SMU_POLL_US			1000U
#define SMU_FIRMWARE_POLLS		1000U
#define THERMAL_SPIN_POLLS		50U
#define THERMAL_SPIN_POLL_US		100000U
#define THERMAL_CHECK_POLLS		50U
#define THERMAL_CHECK_POLL_US		10000U
#define THERMAL_WATCH_TICKS		KERN_CLOCK_HZ

/*
 * The defaults: the static duty (50 %), the temperature limit (95 degrees
 * Celsius), how far above the temperature the interrupt check puts the
 * low threshold (20 degrees), and the high alert when the video BIOS
 * gives no shutdown temperature (10 degrees above the limit).
 */
#define THERMAL_DEFAULT_DUTY		50U
#define THERMAL_DEFAULT_LIMIT		95U
#define THERMAL_CHECK_ABOVE		20U
#define THERMAL_HIGH_ABOVE_LIMIT	10U

/* How long the stop waits for the watch thread, and its step. */
#define THERMAL_STOP_WAIT_US		2000000U
#define THERMAL_STOP_STEP_US		10000U

static int power_firmware_running(struct amdrdna_device *device);
static int power_versions(struct amdrdna_device *device);
static int power_fan(struct amdrdna_device *device);
static void power_alerts(struct amdrdna_device *device);
static bool power_interrupt_check(struct amdrdna_device *device);
static void power_spin_up(struct amdrdna_device *device);
static void power_read(struct amdrdna_device *device);
static void power_fault(struct amdrdna_device *device, const char *why);
static int power_watch_start(struct amdrdna_device *device);
static void power_watch(void *argument);
static int smu_message(struct amdrdna_device *device, uint32_t message, uint32_t argument, uint32_t *answer);
static uint32_t smu_wait_answer(struct amdrdna_device *device);
static uint32_t thermal_read(struct amdrdna_device *device, uint32_t offset);
static void thermal_write(struct amdrdna_device *device, uint32_t offset, uint32_t value);

/*
 * Runs stage A4 in its static tier: checks that the unit's firmware runs
 * and asks its versions, drives the fan at the static duty, sends the
 * thermal alerts to the interrupt ring and checks that one arrives, waits
 * for the fan to turn, reads the temperature, and starts the watch.
 *
 * Returns 0, ENODEV when the block table lacks the unit or the thermal
 * block, ETIMEDOUT when the unit's firmware does not run or does not
 * answer, or EINVAL when the fan controller cannot be driven.
 */
int
amdrdna_a4_power(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	const char *path;
	uint32_t index;
	bool delivered;
	int error;

	/* Says the stage starts. */
	amdrdna_mark("A4 begin");
	thermal = &device->thermal;

	/* The unit's mailbox and the thermal block must both be listed. */
	error = amdrdna_block_index(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_MESSAGE, &index);
	if (error == 0)
		error = amdrdna_block_index(device, AMDRDNA_BLOCK_THM, AMDRDNA_THM_WINDOW_MAIN, AMDRDNA_THM_REG_TEMPERATURE, &index);
	if (error != 0) {
		amdrdna_mark("A4 fail: the block table lists no smu or no thermal block");
		return ENODEV;
	}

	/* The unit's firmware must run. */
	error = power_firmware_running(device);
	if (error != 0)
		return error;

	/* Asks its versions. */
	error = power_versions(device);
	if (error != 0)
		return error;

	/* Drives the fan at the static duty. */
	error = power_fan(device);
	if (error != 0)
		return error;

	/* Sends the alerts to the interrupt ring, and checks that one arrives. */
	power_alerts(device);
	delivered = power_interrupt_check(device);

	/* Waits for the fan to turn, and reads the temperature and the fan. */
	power_spin_up(device);
	power_read(device);

	/* Starts the watch. */
	error = power_watch_start(device);
	if (error != 0) {
		amdrdna_mark("A4 fail: the watch thread not started (%d)", error);
		return error;
	}

	/* Names whether the interrupt check saw its alert. */
	path = "none";
	if (delivered)
		path = "thm";

	/* Marks the result. */
	amdrdna_mark("A4 ok if=0x%x smu=%u.%u.%u mode=S duty=%u%% fan=%urpm driven=%u%% temp=%uC max=%uC limit=%uC alert=%uC irq=%s",
	    (unsigned)thermal->interface_version,
	    (unsigned)((thermal->smu_version >> 16) & 0xffU),
	    (unsigned)((thermal->smu_version >> 8) & 0xffU),
	    (unsigned)(thermal->smu_version & 0xffU),
	    thermal->duty_percent,
	    (unsigned)thermal->fan_speed,
	    thermal->fan_driven,
	    thermal->temperature,
	    thermal->highest,
	    thermal->limit,
	    thermal->high_alert,
	    path);

	/* Succeeded: the fan turns at its duty and the driver watches the temperature. */
	return 0;
}

/*
 * Undoes stage A4 for the shutdown: stops the watch thread, masks the
 * thermal alerts and the unit's software interrupt, and tells the unit's
 * firmware it is about to be unloaded.  The fan keeps its static duty;
 * the next post of the video BIOS sets it again.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c (smu_v11_0_set_irq_state,
 * the disable case) and navi10_ppt.c (PrepareMp1ForUnload).
 */
void
amdrdna_power_stop(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	unsigned waited;
	uint32_t word;
	bool running;
	int error;

	/* Nothing to undo before the stage ran. */
	thermal = &device->thermal;
	if (thermal->mode == AMDRDNA_FAN_NONE)
		return;

	/* Tells the watch thread to stop. */
	irq = spin_lock_irqsave(&thermal->lock);

	thermal->stopping = true;
	waitq_wake_all(&thermal->waitq);

	spin_unlock_irqrestore(&thermal->lock, irq);

	/* Waits for it to end, for a bounded time. */
	for (waited = 0U; waited < THERMAL_STOP_WAIT_US; waited += THERMAL_STOP_STEP_US) {
		/* Reads whether the thread still runs. */
		irq = spin_lock_irqsave(&thermal->lock);

		running = thermal->thread_running;

		spin_unlock_irqrestore(&thermal->lock, irq);

		/* Stops waiting once it has ended. */
		if (!running)
			break;

		/* Gives the thread a step to finish. */
		kern_usleep_range(THERMAL_STOP_STEP_US, THERMAL_STOP_STEP_US * 2U);
	}

	/* Masks the thermal alerts and the unit's software interrupt. */
	if (thermal->alerts_on) {
		word = thermal_read(device, AMDRDNA_THM_REG_ALERT_CONTROL);
		thermal_write(device, AMDRDNA_THM_REG_ALERT_CONTROL, amdrdna_alert_off_word(word));
		thermal_write(device, AMDRDNA_THM_REG_ALERT_ENABLE, 0U);
		word = amdrdna_block_read(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL);
		word |= AMDRDNA_SMU_SOFT_INTERRUPT_MASKED;
		amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL, word);
		thermal->alerts_on = false;
	}

	/* Tells the unit's firmware it is about to be unloaded. */
	error = smu_message(device, SMU_MESSAGE_UNLOAD, 0U, NULL);

	/* Marks the undo, for the log, with the unload's answer. */
	amdrdna_mark("shutdown: A4 undone, watch stopped, alerts masked, smu unload answered %d", error);
}

/*
 * Handles an interrupt of the thermal side: a high alert (the temperature
 * rose past the high threshold) faults the GPU, a low alert is counted
 * (the interrupt check raises one), the critical-temperature trip faults
 * the GPU, and the unit's software interrupt is acknowledged and counted.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c (smu_v11_0_irq_process;
 * the driver faults the GPU instead of shutting the system down).
 */
void
amdrdna_thermal_interrupt(
	struct amdrdna_device *device,
	const struct amdrdna_interrupt *interrupt)
{
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	uint32_t word;

	/* The power side's state. */
	thermal = &device->thermal;

	/* Each source of the three clients. */
	if (interrupt->client == AMDRDNA_CLIENT_THERMAL && interrupt->source == AMDRDNA_SOURCE_THERMAL_HIGH) {
		/* Counts the high alert. */
		irq = spin_lock_irqsave(&thermal->lock);

		thermal->high_alerts++;

		spin_unlock_irqrestore(&thermal->lock, irq);

		/* Faults the GPU. */
		power_fault(device, "the thermal block's high alert");
	} else if (interrupt->client == AMDRDNA_CLIENT_THERMAL && interrupt->source == AMDRDNA_SOURCE_THERMAL_LOW) {
		/* Counts the low alert. */
		irq = spin_lock_irqsave(&thermal->lock);

		thermal->low_alerts++;

		spin_unlock_irqrestore(&thermal->lock, irq);
	} else if (interrupt->client == AMDRDNA_CLIENT_SMU_IO && interrupt->source == AMDRDNA_SOURCE_CRITICAL) {
		/* Counts the trip. */
		irq = spin_lock_irqsave(&thermal->lock);

		thermal->critical_alerts++;

		spin_unlock_irqrestore(&thermal->lock, irq);

		/* Faults the GPU. */
		power_fault(device, "the critical-temperature trip");
	} else if (interrupt->client == AMDRDNA_CLIENT_SMU && interrupt->source == AMDRDNA_SOURCE_SMU_SOFT) {
		/* Acknowledges the software interrupt so the unit can raise the next. */
		word = amdrdna_block_read(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL);
		word |= AMDRDNA_SMU_SOFT_INTERRUPT_ACKNOWLEDGE;
		amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL, word);

		/* Counts it. */
		irq = spin_lock_irqsave(&thermal->lock);

		thermal->smu_interrupts++;

		spin_unlock_irqrestore(&thermal->lock, irq);

		/* Names its context in the log. */
		amdrdna_mark("smu: software interrupt context 0x%x", (unsigned)interrupt->data[0]);
	} else {
		/* A source of these clients the stage does not know. */
		amdrdna_mark("thermal: unhandled client=0x%x source=0x%x", (unsigned)interrupt->client, (unsigned)interrupt->source);
	}
}

/*
 * Waits for the unit's firmware to say it runs: bit 0 of its state word,
 * read through the indirect register window.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_check_fw_status).
 */
static int
power_firmware_running(
	struct amdrdna_device *device)
{
	uint32_t word;
	unsigned poll;

	/* Looks each millisecond until the bit is set. */
	word = 0U;
	for (poll = 0U; poll < SMU_FIRMWARE_POLLS; poll++) {
		/* Stops once the firmware runs. */
		word = amdrdna_far_read(device, AMDRDNA_SMU_FAR_FIRMWARE_STATE);
		if ((word & AMDRDNA_SMU_FIRMWARE_RUNNING) != 0U)
			break;

		/* Sleeps one step. */
		kern_usleep_range(SMU_POLL_US, SMU_POLL_US + SMU_POLL_US / 4U);
	}

	/* Reports a firmware that does not run. */
	if ((word & AMDRDNA_SMU_FIRMWARE_RUNNING) == 0U) {
		amdrdna_mark("A4 fail: the smu firmware does not run (state 0x%08x)", (unsigned)word);
		return ETIMEDOUT;
	}

	/* Records the state word. */
	device->smu_state = word;

	/* Succeeded: the firmware runs. */
	return 0;
}

/*
 * Asks the unit's firmware for the driver-interface version it implements
 * and its own version.  A different interface version is only logged:
 * the firmware keeps the older interfaces.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_check_fw_version) and smu_cmn.c (smu_cmn_get_smc_version).
 */
static int
power_versions(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	int error;

	/* The power side's state. */
	thermal = &device->thermal;

	/* The interface version. */
	error = smu_message(device, SMU_MESSAGE_INTERFACE, 0U, &thermal->interface_version);
	if (error != 0) {
		amdrdna_mark("A4 fail: the smu does not answer the interface version (%d)", error);
		return error;
	}

	/* The firmware version. */
	error = smu_message(device, SMU_MESSAGE_VERSION, 0U, &thermal->smu_version);
	if (error != 0) {
		amdrdna_mark("A4 fail: the smu does not answer its version (%d)", error);
		return error;
	}

	/* Logs an interface version other than the one the driver was written against. */
	if (thermal->interface_version != SMU_INTERFACE_EXPECTED) {
		amdrdna_mark("A4 note: smu interface 0x%x, the driver expects 0x%x",
		    (unsigned)thermal->interface_version,
		    (unsigned)SMU_INTERFACE_EXPECTED);
	}

	/* Succeeded: both versions are read. */
	return 0;
}

/*
 * Drives the fan at the static duty (amdrdna.fan.duty=, 50 % by default):
 * the duty word first, then the mode word that makes the controller use
 * it.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_set_fan_speed_pwm, smu_v11_0_set_fan_static_mode).
 */
static int
power_fan(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	uint32_t limits;
	uint32_t old;
	uint32_t word;
	unsigned percent;
	int error;

	/* The power side's state. */
	thermal = &device->thermal;

	/* The duty asked for, at most 100 %. */
	percent = amdrdna_parameter_number("amdrdna.fan.duty", THERMAL_DEFAULT_DUTY);
	if (percent > 100U)
		percent = 100U;

	/* The duty word for that percentage of the controller's full duty. */
	limits = thermal_read(device, AMDRDNA_THM_REG_FAN_LIMITS);
	old = thermal_read(device, AMDRDNA_THM_REG_FAN_DUTY);
	error = amdrdna_fan_duty_word(old, limits, percent, &word);
	if (error != 0) {
		amdrdna_mark("A4 fail: the fan controller has no full duty (limits 0x%08x)", (unsigned)limits);
		return EINVAL;
	}

	/* Writes the duty, then the static mode. */
	thermal_write(device, AMDRDNA_THM_REG_FAN_DUTY, word);
	old = thermal_read(device, AMDRDNA_THM_REG_FAN_MODE);
	thermal_write(device, AMDRDNA_THM_REG_FAN_MODE, amdrdna_fan_mode_word(old));

	/* The fan is the driver's from now on. */
	thermal->duty_percent = percent;
	thermal->mode = AMDRDNA_FAN_STATIC;
	amdrdna_mark("A4 fan duty %u%% (limits 0x%08x, duty 0x%08x, mode 0x%08x)",
	    percent,
	    (unsigned)limits,
	    (unsigned)thermal_read(device, AMDRDNA_THM_REG_FAN_DUTY),
	    (unsigned)thermal_read(device, AMDRDNA_THM_REG_FAN_MODE));

	/* Succeeded: the controller drives the static duty. */
	return 0;
}

/*
 * Sends the thermal alerts to the interrupt ring: high at the board's
 * shutdown temperature from the video BIOS (or 10 degrees above the
 * driver's limit without one), low at 0; and lets the unit raise its
 * software interrupt.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c (smu_v11_0_set_irq_state,
 * the enable case).
 */
static void
power_alerts(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	uint32_t word;

	/* The power side's state. */
	thermal = &device->thermal;

	/* The driver's limit and the high alert's threshold. */
	thermal->limit = amdrdna_parameter_number("amdrdna.temp.max", THERMAL_DEFAULT_LIMIT);
	thermal->high_alert = device->vbios_info.shutdown_temperature;
	if (thermal->high_alert == 0U)
		thermal->high_alert = thermal->limit + THERMAL_HIGH_ABOVE_LIMIT;
	if (thermal->high_alert > 255U)
		thermal->high_alert = 255U;

	/* The thresholds and the switch to the ring, then the clears. */
	word = thermal_read(device, AMDRDNA_THM_REG_ALERT_CONTROL);
	thermal_write(device, AMDRDNA_THM_REG_ALERT_CONTROL, amdrdna_alert_word(word, thermal->high_alert, 0U));
	thermal_write(device, AMDRDNA_THM_REG_ALERT_ENABLE, amdrdna_alert_clear_word());

	/* The unit's software interrupt: its ID, not raised, and unmasked. */
	word = amdrdna_block_read(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT);
	word &= ~(AMDRDNA_SMU_SOFT_INTERRUPT_ID_MASK | AMDRDNA_SMU_SOFT_INTERRUPT_VALID);
	word |= AMDRDNA_SOURCE_SMU_SOFT;
	amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT, word);
	word = amdrdna_block_read(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL);
	word &= ~AMDRDNA_SMU_SOFT_INTERRUPT_MASKED;
	amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL, word);
	thermal->alerts_on = true;

	/* Marks the alerts. */
	amdrdna_mark("A4 alerts high=%uC low=0C control=0x%08x",
	    thermal->high_alert,
	    (unsigned)thermal_read(device, AMDRDNA_THM_REG_ALERT_CONTROL));
}

/*
 * Checks that an interrupt reaches the driver: raises the low alert's
 * threshold 20 degrees above the temperature, which makes the thermal
 * block send a low alert, waits up to 500 ms for the interrupt thread to
 * count it, and puts the threshold back at 0.  Reports whether one came.
 */
static bool
power_interrupt_check(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	uint64_t before;
	uint64_t after;
	uint64_t signals;
	unsigned sensor;
	unsigned highest;
	unsigned poll;
	uint32_t word;

	/* The power side's state. */
	thermal = &device->thermal;

	/* The count of low alerts and messages before the check. */
	irq = spin_lock_irqsave(&thermal->lock);

	before = thermal->low_alerts;

	spin_unlock_irqrestore(&thermal->lock, irq);
	signals = device->interrupts.signals;

	/* Raises the low threshold above the temperature. */
	amdrdna_temperature_read(thermal_read(device, AMDRDNA_THM_REG_TEMPERATURE), &sensor, &highest);
	word = thermal_read(device, AMDRDNA_THM_REG_ALERT_CONTROL);
	thermal_write(device, AMDRDNA_THM_REG_ALERT_CONTROL, amdrdna_alert_word(word, thermal->high_alert, sensor + THERMAL_CHECK_ABOVE));

	/* Waits for the alert to be counted. */
	after = before;
	for (poll = 0U; poll < THERMAL_CHECK_POLLS; poll++) {
		/* Sleeps one step, then reads the count. */
		kern_usleep_range(THERMAL_CHECK_POLL_US, THERMAL_CHECK_POLL_US + THERMAL_CHECK_POLL_US / 4U);
		irq = spin_lock_irqsave(&thermal->lock);

		after = thermal->low_alerts;

		spin_unlock_irqrestore(&thermal->lock, irq);

		/* Stops once it came. */
		if (after != before)
			break;
	}

	/* Puts the low threshold back at 0. */
	word = thermal_read(device, AMDRDNA_THM_REG_ALERT_CONTROL);
	thermal_write(device, AMDRDNA_THM_REG_ALERT_CONTROL, amdrdna_alert_word(word, thermal->high_alert, 0U));
	thermal_write(device, AMDRDNA_THM_REG_ALERT_ENABLE, amdrdna_alert_clear_word());

	/* Marks what came: the alerts, the messages and the entries. */
	amdrdna_mark("A4 irq check low=%uC: low alerts %llu -> %llu, messages %llu -> %llu, entries %llu, unknown %llu",
	    sensor + THERMAL_CHECK_ABOVE,
	    (unsigned long long)before,
	    (unsigned long long)after,
	    (unsigned long long)signals,
	    (unsigned long long)device->interrupts.signals,
	    (unsigned long long)device->interrupts.entries,
	    (unsigned long long)device->interrupts.unknown);

	/* Reports a missing alert. */
	if (after == before)
		return false;

	/* Succeeded: the alert came through the ring. */
	return true;
}

/* Waits up to 5 s for the fan's tachometer to count turns. */
static void
power_spin_up(
	struct amdrdna_device *device)
{
	uint32_t speed;
	unsigned poll;

	/* Looks every 100 ms until the fan turns. */
	for (poll = 0U; poll < THERMAL_SPIN_POLLS; poll++) {
		/* Stops once the tachometer counts turns. */
		speed = amdrdna_fan_speed(thermal_read(device, AMDRDNA_THM_REG_FAN_TACHOMETER));
		if (speed != 0U)
			return;

		/* Sleeps one step. */
		kern_usleep_range(THERMAL_SPIN_POLL_US, THERMAL_SPIN_POLL_US + THERMAL_SPIN_POLL_US / 10U);
	}
}

/*
 * Reads the temperature and the fan and publishes them; faults the GPU
 * past the limit.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/powerplay/hwmgr/vega10_thermal.c
 * (vega10_thermal_get_temperature) and
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_get_fan_speed_rpm, smu_v11_0_get_fan_speed_pwm).
 */
static void
power_read(
	struct amdrdna_device *device)
{
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	unsigned sensor;
	unsigned highest;
	uint32_t speed;
	unsigned driven;
	uint32_t limits;

	/* The power side's state. */
	thermal = &device->thermal;

	/* Reads the thermal block. */
	amdrdna_temperature_read(thermal_read(device, AMDRDNA_THM_REG_TEMPERATURE), &sensor, &highest);
	speed = amdrdna_fan_speed(thermal_read(device, AMDRDNA_THM_REG_FAN_TACHOMETER));
	limits = thermal_read(device, AMDRDNA_THM_REG_FAN_LIMITS);
	driven = amdrdna_fan_driven(thermal_read(device, AMDRDNA_THM_REG_FAN_STATUS), limits);

	/* Publishes the readings. */
	irq = spin_lock_irqsave(&thermal->lock);

	thermal->temperature = sensor;
	thermal->highest = highest;
	thermal->fan_speed = speed;
	thermal->fan_driven = driven;

	spin_unlock_irqrestore(&thermal->lock, irq);

	/* Past the limit the GPU is faulted. */
	if (sensor >= thermal->limit)
		power_fault(device, "the temperature reached the limit");
}

/*
 * Faults the GPU for a thermal reason: marks it once and logs why.  The
 * GPU takes no more work; the system keeps running.
 */
static void
power_fault(
	struct amdrdna_device *device,
	const char *why)
{
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	bool first;

	/* The power side's state. */
	thermal = &device->thermal;

	/* Marks the fault, noting whether it is the first. */
	irq = spin_lock_irqsave(&thermal->lock);

	first = !thermal->faulted;
	thermal->faulted = true;

	spin_unlock_irqrestore(&thermal->lock, irq);

	/* Logs the first fault with the readings. */
	if (first) {
		amdrdna_mark("thermal fault: %s (temp %uC, limit %uC, fan %urpm); the GPU takes no more work",
		    why,
		    thermal->temperature,
		    thermal->limit,
		    (unsigned)thermal->fan_speed);
	}
}

/* Starts the watch thread. */
static int
power_watch_start(
	struct amdrdna_device *device)
{
	struct thread *thread;
	int error;

	/* Creates the thread. */
	error = kthread_create(power_watch, device, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0)
		return error;

	/*
	 * thread_running tells the stop that the thread may still read the
	 * thermal block; the thread clears it last.  Nobody joins the thread.
	 */
	device->thermal.thread_running = true;
	thread->detached = 1;
	thread_start(thread);

	/* Succeeded: the watch runs. */
	return 0;
}

/* Reads the temperature and the fan once a second until told to stop. */
static void
power_watch(
	void *argument)
{
	struct amdrdna_device *device;
	struct amdrdna_thermal *thermal;
	unsigned long irq;
	uint64_t sequence;
	bool stopping;

	/* The GPU the thread watches. */
	device = argument;
	thermal = &device->thermal;

	/* One reading per turn of the loop. */
	for (;;) {
		/* Sleeps one period, or until told to stop. */
		irq = spin_lock_irqsave(&thermal->lock);

		if (!thermal->stopping) {
			sequence = waitq_sequence(&thermal->waitq);
			(void)waitq_sleep(&thermal->waitq, &thermal->lock, sequence, sched_ticks() + THERMAL_WATCH_TICKS, 0U);
		}

		/* Notes whether the stop came. */
		stopping = thermal->stopping;

		spin_unlock_irqrestore(&thermal->lock, irq);

		/* Leaves the loop when told to stop. */
		if (stopping)
			break;

		/* Reads and checks. */
		power_read(device);
	}

	/* Tells the stop that the thread no longer reads the block. */
	irq = spin_lock_irqsave(&thermal->lock);

	thermal->thread_running = false;

	spin_unlock_irqrestore(&thermal->lock, irq);
}

/*
 * Sends one message to the unit's firmware and waits for its answer: all
 * but the first message wait for the previous answer first; the answer
 * word is cleared, the argument and the message written, and the answer
 * waited for.  On OK the output argument is read.  A unit that does not
 * answer is marked hung and sent nothing more.
 *
 * Returns 0, EIO for a failed or hung unit, EINVAL for a message it does
 * not know, EAGAIN for a missing prerequisite, EBUSY, or ETIMEDOUT.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu_cmn.c (smu_cmn_send_smc_msg_with_param,
 * __smu_cmn_send_msg, __smu_cmn_poll_stat, __smu_cmn_reg2errno,
 * smu_cmn_read_arg).
 */
static int
smu_message(
	struct amdrdna_device *device,
	uint32_t message,
	uint32_t argument,
	uint32_t *answer)
{
	struct amdrdna_thermal *thermal;
	uint32_t word;
	int error;

	/* The power side's state. */
	thermal = &device->thermal;

	/* One message at a time. */
	mutex_lock(&thermal->mailbox);

	/* A hung unit is sent nothing. */
	if (thermal->hung) {
		mutex_unlock(&thermal->mailbox);
		return EIO;
	}

	/* After the first message, the previous answer must have come. */
	if (thermal->first_sent) {
		word = smu_wait_answer(device);
		if (word == 0U) {
			mutex_unlock(&thermal->mailbox);
			amdrdna_mark("smu: no answer to the previous message before 0x%x", (unsigned)message);
			return ETIMEDOUT;
		}
	}

	/* Clears the answer, writes the argument, then the message. */
	amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_RESPONSE, 0U);
	amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_ARGUMENT, argument);
	amdrdna_block_write(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_MESSAGE, message);
	thermal->first_sent = true;

	/* Waits for the answer and turns it into an error. */
	word = smu_wait_answer(device);
	switch (word) {
	case SMU_ANSWER_OK:
		error = 0;
		break;
	case SMU_ANSWER_FAILED:
		error = EIO;
		break;
	case SMU_ANSWER_UNKNOWN:
		error = EINVAL;
		break;
	case SMU_ANSWER_PREREQUISITE:
		error = EAGAIN;
		break;
	case SMU_ANSWER_BUSY:
		error = EBUSY;
		break;
	case 0U:
		error = ETIMEDOUT;
		thermal->hung = true;
		break;
	default:
		error = EIO;
		thermal->hung = true;
		break;
	}

	/* Reads the output argument of an answered message. */
	if (error == 0 && answer != NULL)
		*answer = amdrdna_block_read(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_ARGUMENT);

	mutex_unlock(&thermal->mailbox);

	/* Reports a message the unit did not carry out. */
	if (error != 0) {
		amdrdna_mark("smu: message 0x%x answered 0x%x (%d)", (unsigned)message, (unsigned)word, error);
		return error;
	}

	/* Succeeded: the unit carried the message out. */
	return 0;
}

/* Waits for a nonzero answer word, each millisecond for 2 s; 0 when none came. */
static uint32_t
smu_wait_answer(
	struct amdrdna_device *device)
{
	uint32_t word;
	unsigned poll;

	/* Looks, then sleeps, until the word is set. */
	word = 0U;
	for (poll = 0U; poll < SMU_ANSWER_POLLS; poll++) {
		/* Stops at the answer. */
		word = amdrdna_block_read(device, AMDRDNA_BLOCK_SMU, AMDRDNA_SMU_WINDOW_MAILBOX, AMDRDNA_SMU_REG_RESPONSE);
		if (word != 0U)
			break;

		/* Sleeps one step. */
		kern_usleep_range(SMU_POLL_US, SMU_POLL_US + SMU_POLL_US / 4U);
	}

	/* The answer, or 0. */
	return word;
}

/* Reads one register of the thermal block. */
static uint32_t
thermal_read(
	struct amdrdna_device *device,
	uint32_t offset)
{
	uint32_t value;

	/* The registers are in the block's first window. */
	value = amdrdna_block_read(device, AMDRDNA_BLOCK_THM, AMDRDNA_THM_WINDOW_MAIN, offset);

	/* Succeeded: the register's value. */
	return value;
}

/* Writes one register of the thermal block. */
static void
thermal_write(
	struct amdrdna_device *device,
	uint32_t offset,
	uint32_t value)
{
	/* The registers are in the block's first window. */
	amdrdna_block_write(device, AMDRDNA_BLOCK_THM, AMDRDNA_THM_WINDOW_MAIN, offset, value);
}
