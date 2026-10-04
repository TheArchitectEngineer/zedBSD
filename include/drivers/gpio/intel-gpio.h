/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pads of the Intel PCH's GPIO controller (INTC1055 and its kin) that
 * ACPI GpioInt resources name (ws159-p006): a pad found from its
 * controller's path and its ACPI pin, and the level of its input.
 */

#ifndef DRIVERS_GPIO_INTEL_GPIO_H
#define DRIVERS_GPIO_INTEL_GPIO_H

#include <stdint.h>

struct drv_intel_gpio_pad;

int drv_intel_gpio_pad_find(const char *controller, uint32_t pin, struct drv_intel_gpio_pad **result);
int drv_intel_gpio_pad_level(const struct drv_intel_gpio_pad *pad);

#endif
