/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The activity LED of the Raspberry Pi 4 (GPIO 42, lit when high), used to
 * count the stages of the early boot in blinks, so that how far a board
 * gets can be seen without a serial console or a working display.
 */

#include <hal/hal.h>
#include "../defs.h"
#include "led.h"

#if defined(HAL_BOARD_RPI5)
/*
 * The Pi 5's activity LED: pin 9 of the first bank of the always-on GPIO
 * block, lit when the pin is driven low.  A bank's data register holds the
 * pin levels and its direction register a one for each input.
 *
 * Values referenced: Raspberry Pi Linux rpi-6.12.y
 * arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts (led_act on gio_aon 9,
 * active low) and drivers/gpio/gpio-brcmstb.c (the bank's data and
 * direction register offsets).
 */
#define GPIO_BASE		RPI5_AON_GPIO_BASE
#define AON_GPIO_DATA		0x04U
#define AON_GPIO_IODIR		0x08U
#define LED_BIT			(1U << 9)
#else
/* The GPIO block (the default low-peripheral address) and its registers. */
#define GPIO_BASE	0xfe200000ULL
#define GPFSEL4		0x10U
#define GPSET1		0x20U
#define GPCLR1		0x2cU

/* GPIO 42: bits 6 to 8 of GPFSEL4, and bit 10 of the second set and clear. */
#define LED_FUNCTION_SHIFT	6U
#define LED_BIT			(1U << 10)
#endif

/* How long a blink is lit and dark, and the pause after a stage. */
#define BLINK_ON_MS		250U
#define BLINK_OFF_MS		350U
#define STAGE_PAUSE_MS		1200U

static volatile uint8_t *gpio_registers(void);
static uint64_t counter(void);
static uint64_t counter_frequency(void);

#if defined(HAL_BOARD_RPI5)
/*
 * Makes the activity LED's pin an output, and turns the LED off.
 */
void
rpi4_led_init(
	void)
{
	volatile uint8_t *base;
	uint32_t direction;

	/* Dark first, so the pin does not light it when it turns into an output. */
	rpi4_led_set(0);

	/* Clears the pin's input bit: the block drives it from now on. */
	base = gpio_registers();
	direction = hal_mmio_read32(base + AON_GPIO_IODIR);
	direction &= ~LED_BIT;
	hal_mmio_write32(base + AON_GPIO_IODIR, direction);
}

/*
 * Turns the LED on or off.
 */
void
rpi4_led_set(
	int on)
{
	volatile uint8_t *base;
	uint32_t levels;

	/* Reads the bank's levels, which the other pins keep. */
	base = gpio_registers();
	levels = hal_mmio_read32(base + AON_GPIO_DATA);

	/* A low pin lights the LED, and a high one darkens it. */
	if (on) {
		levels &= ~LED_BIT;
	} else {
		levels |= LED_BIT;
	}
	hal_mmio_write32(base + AON_GPIO_DATA, levels);
}

#else
/*
 * Makes GPIO 42 an output, and turns the LED off.
 */
void
rpi4_led_init(
	void)
{
	volatile uint8_t *base;
	uint32_t select;

	/* The function of pin 42: output (001). */
	base = gpio_registers();
	select = hal_mmio_read32(base + GPFSEL4);
	select &= ~(7U << LED_FUNCTION_SHIFT);
	select |= 1U << LED_FUNCTION_SHIFT;
	hal_mmio_write32(base + GPFSEL4, select);

	/* Dark until a stage is shown. */
	rpi4_led_set(0);
}

/*
 * Turns the LED on or off.
 */
void
rpi4_led_set(
	int on)
{
	volatile uint8_t *base;

	/* The set register lights it, and the clear register darkens it. */
	base = gpio_registers();
	if (on)
		hal_mmio_write32(base + GPSET1, LED_BIT);
	else
		hal_mmio_write32(base + GPCLR1, LED_BIT);
}

#endif

/*
 * Shows a stage of the boot: as many blinks as its number, then a pause.
 */
void
rpi4_led_stage(
	unsigned stage)
{
	unsigned blink;

	/* The blinks. */
	for (blink = 0; blink < stage; blink++) {
		rpi4_led_set(1);
		rpi4_delay_ms(BLINK_ON_MS);
		rpi4_led_set(0);
		rpi4_delay_ms(BLINK_OFF_MS);
	}

	/* The pause that separates one stage from the next. */
	rpi4_delay_ms(STAGE_PAUSE_MS);
}

/*
 * Waits for a number of milliseconds on the generic timer.
 */
void
rpi4_delay_ms(
	unsigned milliseconds)
{
	uint64_t frequency;
	uint64_t deadline;
	uint64_t now;

	/* The count the wait ends at. */
	frequency = counter_frequency();
	now = counter();
	deadline = now + frequency / 1000U * milliseconds;

	/* Until the counter reaches it. */
	while (now < deadline)
		now = counter();
}

/* Returns the GPIO registers in the kernel's direct map. */
static volatile uint8_t *
gpio_registers(
	void)
{
	/* The physical block through the direct map. */
	return (volatile uint8_t *)(ARM64_DIRECT_BASE + GPIO_BASE);
}

/* Returns the physical count of the generic timer. */
static uint64_t
counter(
	void)
{
	uint64_t value;

	/* CNTPCT_EL0. */
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(value));
	return value;
}

/* Returns the frequency of the generic timer. */
static uint64_t
counter_frequency(
	void)
{
	uint64_t value;

	/* CNTFRQ_EL0. */
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
	return value;
}
