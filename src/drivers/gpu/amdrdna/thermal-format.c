/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The words of the GPU's thermal block: the temperature it reads, the fan
 * controller's duty and mode, the fan's speed from its tachometer, and the
 * thresholds of the thermal alerts.
 *
 * Nothing here touches the hardware: every function turns words read from
 * the block into numbers, or numbers into words to write, so the same code
 * runs in the kernel and in the host tests.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/*
 * The temperature word: the highest reading in bits 8:0 and the reading
 * the trip compares in bits 17:9, both in degrees Celsius.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/thm/thm_11_0_2_sh_mask.h
 * (CG_MULT_THERMAL_STATUS__ASIC_MAX_TEMP_MASK,
 * CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK,
 * CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT).
 */
#define FORMAT_TEMPERATURE_HIGHEST_MASK		0x000001ffU
#define FORMAT_TEMPERATURE_SENSOR_MASK		0x0003fe00U
#define FORMAT_TEMPERATURE_SENSOR_SHIFT		9U

/*
 * The fan controller's words: the static duty in bits 7:0 of the duty
 * word, the duty that stands for 100 % in bits 7:0 of the limits word,
 * the lowest temperature in bits 7:0 and the mode in bits 13:11 of the
 * mode word (1 drives the static duty), and the duty actually driven in
 * bits 16:9 of the status word.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/thm/thm_11_0_2_sh_mask.h
 * (CG_FDO_CTRL0__FDO_STATIC_DUTY_MASK, CG_FDO_CTRL1__FMAX_DUTY100_MASK,
 * CG_FDO_CTRL2__TMIN_MASK, CG_FDO_CTRL2__FDO_PWM_MODE_MASK,
 * CG_FDO_CTRL2__FDO_PWM_MODE__SHIFT, CG_THERMAL_STATUS__FDO_PWM_DUTY_MASK,
 * CG_THERMAL_STATUS__FDO_PWM_DUTY__SHIFT) and
 * drivers/gpu/drm/amd/pm/powerplay/hwmgr/vega10_thermal.h
 * (FDO_PWM_MODE_STATIC, the value smu_v11_0.c writes).
 */
#define FORMAT_FAN_DUTY_MASK			0x000000ffU
#define FORMAT_FAN_FULL_MASK			0x000000ffU
#define FORMAT_FAN_LOWEST_MASK			0x000000ffU
#define FORMAT_FAN_MODE_MASK			0x00003800U
#define FORMAT_FAN_MODE_SHIFT			11U
#define FORMAT_FAN_MODE_STATIC			1U
#define FORMAT_FAN_DRIVEN_MASK			0x0001fe00U
#define FORMAT_FAN_DRIVEN_SHIFT			9U

/*
 * The tachometer counts the crystal's 25 MHz ticks of one fan period; the
 * speed in turns per minute is 25 MHz times 60 over the count.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_get_fan_speed_rpm: 2500 in 10 kHz units, times 60 and
 * 10000).
 */
#define FORMAT_TACHOMETER_TICKS_PER_MINUTE	1500000000ULL

/*
 * The alert control word: the high threshold in bits 7:0, the low one in
 * bits 15:8, the masks of the high, low and trigger alerts in bits 24, 25
 * and 26, the switch that sends the alerts to the interrupt controller in
 * bit 28 and the controller credits in bits 31:29 (5); and the enable
 * word's bits that clear the three alerts (3, 4 and 5).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/thm/thm_11_0_2_sh_mask.h
 * (THM_THERMAL_INT_CTRL__DIG_THERM_INTH_MASK,
 * THM_THERMAL_INT_CTRL__DIG_THERM_INTL_MASK,
 * THM_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK,
 * THM_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK,
 * THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK,
 * THM_THERMAL_INT_CTRL__THERM_IH_HW_ENA_MASK,
 * THM_THERMAL_INT_CTRL__MAX_IH_CREDIT_MASK,
 * THM_THERMAL_INT_ENA__THERM_INTH_CLR_MASK,
 * THM_THERMAL_INT_ENA__THERM_INTL_CLR_MASK,
 * THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR_MASK) and
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c (smu_v11_0_set_irq_state:
 * 5 credits).
 */
#define FORMAT_ALERT_HIGH_MASK			0x000000ffU
#define FORMAT_ALERT_LOW_MASK			0x0000ff00U
#define FORMAT_ALERT_LOW_SHIFT			8U
#define FORMAT_ALERT_HIGH_MASKED		0x01000000U
#define FORMAT_ALERT_LOW_MASKED			0x02000000U
#define FORMAT_ALERT_TRIGGER_MASKED		0x04000000U
#define FORMAT_ALERT_TO_CONTROLLER		0x10000000U
#define FORMAT_ALERT_CREDITS_MASK		0xe0000000U
#define FORMAT_ALERT_CREDITS			0xa0000000U
#define FORMAT_ALERT_CLEAR_ALL			0x00000038U

/*
 * Reads the temperature word: the reading the trip compares (the one the
 * driver watches) and the highest reading, in degrees Celsius.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/powerplay/hwmgr/vega10_thermal.c
 * (vega10_thermal_get_temperature: the trip's reading is the temperature).
 */
void
amdrdna_temperature_read(
	uint32_t word,
	unsigned *sensor,
	unsigned *highest)
{
	/* The two fields. */
	*sensor = (word & FORMAT_TEMPERATURE_SENSOR_MASK) >> FORMAT_TEMPERATURE_SENSOR_SHIFT;
	*highest = word & FORMAT_TEMPERATURE_HIGHEST_MASK;
}

/*
 * Turns the fan controller's duty word into one with a static duty of a
 * percentage of the controller's full duty.
 *
 * Returns 0 with the word, or EINVAL when the limits word gives no full
 * duty (the controller cannot be driven) or the percentage is over 100.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_set_fan_speed_pwm: a 0 to 255 speed times the full duty over
 * 255).
 */
int
amdrdna_fan_duty_word(
	uint32_t old,
	uint32_t limits,
	unsigned percent,
	uint32_t *word)
{
	uint32_t full;
	uint32_t speed;
	uint32_t duty;

	/* The duty that stands for 100 %. */
	full = limits & FORMAT_FAN_FULL_MASK;
	if (full == 0U)
		return EINVAL;

	/* A percentage over 100 is refused. */
	if (percent > 100U)
		return EINVAL;

	/* The percentage as a 0 to 255 speed, then as the controller's duty. */
	speed = (uint32_t)(percent * 255U / 100U);
	duty = speed * full / 255U;

	/* Replaces the static duty, leaving the other bits. */
	*word = (old & ~FORMAT_FAN_DUTY_MASK) | (duty & FORMAT_FAN_DUTY_MASK);

	/* Succeeded: the word drives the duty asked for. */
	return 0;
}

/*
 * Turns the fan controller's mode word into one that drives the static
 * duty with no lowest temperature.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_set_fan_static_mode).
 */
uint32_t
amdrdna_fan_mode_word(
	uint32_t old)
{
	uint32_t word;

	/* No lowest temperature, and the static mode. */
	word = old & ~FORMAT_FAN_LOWEST_MASK;
	word &= ~FORMAT_FAN_MODE_MASK;
	word |= FORMAT_FAN_MODE_STATIC << FORMAT_FAN_MODE_SHIFT;

	/* Succeeded: the new mode word. */
	return word;
}

/*
 * Reports the fan's speed in turns per minute from its tachometer word;
 * a zero count (no period measured) reads as a stopped fan.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c
 * (smu_v11_0_get_fan_speed_rpm).
 */
uint32_t
amdrdna_fan_speed(
	uint32_t tachometer)
{
	uint64_t speed;

	/* No period means no speed to report. */
	if (tachometer == 0U)
		return 0U;

	/* The ticks of one minute over the ticks of one period. */
	speed = FORMAT_TACHOMETER_TICKS_PER_MINUTE / tachometer;

	/* Succeeded: the speed. */
	return (uint32_t)speed;
}

/*
 * Reports the duty the fan is driven at, in percent of the full duty,
 * from the status word and the limits word.  A controller without a full
 * duty reads as 0.
 */
unsigned
amdrdna_fan_driven(
	uint32_t status,
	uint32_t limits)
{
	uint32_t full;
	uint32_t duty;

	/* Without a full duty there is no percentage. */
	full = limits & FORMAT_FAN_FULL_MASK;
	if (full == 0U)
		return 0U;

	/* The duty driven, as a percentage, rounded to the nearest. */
	duty = (status & FORMAT_FAN_DRIVEN_MASK) >> FORMAT_FAN_DRIVEN_SHIFT;

	/* Succeeded: the percentage. */
	return (unsigned)((duty * 100U + full / 2U) / full);
}

/*
 * Turns the alert control word into one that sends the high and low
 * alerts to the interrupt controller at the thresholds given (degrees
 * Celsius, 0 to 255), with the trigger alert unmasked too.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c (smu_v11_0_set_irq_state,
 * the enable case).
 */
uint32_t
amdrdna_alert_word(
	uint32_t old,
	unsigned high,
	unsigned low)
{
	uint32_t word;

	/* Thresholds over 255 degrees stop at 255. */
	if (high > 255U)
		high = 255U;
	if (low > 255U)
		low = 255U;

	/* The credits and the switch to the controller, the high and low alerts unmasked. */
	word = old & ~FORMAT_ALERT_CREDITS_MASK;
	word |= FORMAT_ALERT_CREDITS;
	word |= FORMAT_ALERT_TO_CONTROLLER;
	word &= ~FORMAT_ALERT_HIGH_MASKED;
	word &= ~FORMAT_ALERT_LOW_MASKED;
	word &= ~FORMAT_ALERT_TRIGGER_MASKED;

	/* The two thresholds. */
	word &= ~(FORMAT_ALERT_HIGH_MASK | FORMAT_ALERT_LOW_MASK);
	word |= (uint32_t)high;
	word |= (uint32_t)low << FORMAT_ALERT_LOW_SHIFT;

	/* Succeeded: the new control word. */
	return word;
}

/*
 * Turns the alert control word into one with the high and low alerts
 * masked.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/smu11/smu_v11_0.c (smu_v11_0_set_irq_state,
 * the disable case).
 */
uint32_t
amdrdna_alert_off_word(
	uint32_t old)
{
	uint32_t word;

	/* Both alerts masked. */
	word = old | FORMAT_ALERT_HIGH_MASKED | FORMAT_ALERT_LOW_MASKED;

	/* Succeeded: the new control word. */
	return word;
}

/*
 * Reports the enable word that clears the high, low and trigger alerts.
 */
uint32_t
amdrdna_alert_clear_word(void)
{
	/* The three clear bits. */
	return FORMAT_ALERT_CLEAR_ALL;
}
