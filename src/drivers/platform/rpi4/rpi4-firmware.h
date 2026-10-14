/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Raspberry Pi 4 firmware property interface.
 *
 * The VideoCore firmware answers requests the ARM side posts through the
 * property channel of the BCM2835 mailbox.  The kernel uses it after boot for
 * what only the firmware can do, such as loading the USB controller's
 * firmware after its PCIe reset and driving the Wi-Fi and Bluetooth power
 * lines of its GPIO expander.
 */

#ifndef KERN_DRIVERS_RPI4_FIRMWARE_H
#define KERN_DRIVERS_RPI4_FIRMWARE_H

#include <stdint.h>

#include <drivers/generic/fdt.h>
#include <drivers/pci/pci.h>

/* Asks the firmware to load the VL805's firmware after a PCIe reset. */
#define DRV_RPI4_FIRMWARE_TAG_NOTIFY_XHCI_RESET	0x00030058U

/* The most value words one request carries. */
#define DRV_RPI4_FIRMWARE_MAX_VALUES		8U

/*
 * The firmware GPIO expander's lines a driver may drive, numbered as the
 * device tree's gpio-line-names of the expander list them.
 *
 * Only these two are accepted: the same expander drives the SD card's
 * power, which must not be touched by mistake.
 */
enum drv_rpi4_firmware_gpio {
	DRV_RPI4_FIRMWARE_GPIO_BT_ON = 0,
	DRV_RPI4_FIRMWARE_GPIO_WL_ON = 1
};

/*
 * The direction of an expander line.  The numbers are not the firmware's
 * codes.
 */
enum drv_rpi4_firmware_gpio_direction {
	DRV_RPI4_FIRMWARE_GPIO_INPUT = 1,
	DRV_RPI4_FIRMWARE_GPIO_OUTPUT = 2
};

/*
 * Finds the mailbox in the device tree and prepares the request buffer.
 * Returns an errno; a call after a successful one does nothing.  Callers on
 * different CPUs may race: one prepares and the others wait for it, and a
 * failed preparation is retried by the next call.
 */
int drv_rpi4_firmware_init(const struct drv_fdt *fdt);

/*
 * Sends one property tag and waits for the answer.
 *
 * values holds request_count words on entry and the answer on return;
 * capacity is how many words the tag's value buffer has.  answered reports
 * how many bytes the firmware wrote.  A notification without values uses
 * request_count=capacity=0 and may pass values=NULL; answered is required.
 * Returns EIO when the firmware refused
 * the request, ENOTSUP when it did not understand the tag and ETIMEDOUT when
 * it did not answer.
 */
int drv_rpi4_firmware_property(
	uint32_t tag,
	uint32_t *values,
	unsigned request_count,
	unsigned capacity,
	uint32_t *answered);

#if defined(CONFIG_DRIVER_BCM2711_VCHIQ) && CONFIG_DRIVER_BCM2711_VCHIQ
/*
 * Posts one word to a mailbox channel other than the property channel.
 *
 * The low four bits of value are replaced by the channel.  The firmware
 * gives no answer on these channels, so the call returns once the word is
 * posted.  Serialized with property requests.  Thread context only.
 */
int drv_rpi4_firmware_mailbox_post(unsigned channel, uint32_t value);
#endif

/*
 * Asks the firmware to load the USB controller's firmware at a PCI address.
 */
int drv_rpi4_firmware_notify_xhci_reset(const struct drv_pci_address *address);

/*
 * Reads whether an expander line is high.
 *
 * state receives 0 or 1.  Returns EINVAL for a line outside the
 * enumeration, without asking the firmware, and EIO when the firmware
 * reports a failure in its answer.  Thread context only.
 */
int drv_rpi4_firmware_get_gpio(enum drv_rpi4_firmware_gpio line, int *state);

/*
 * Drives an expander line low (0) or high (1).
 *
 * Returns EINVAL for a line outside the enumeration or another state,
 * without asking the firmware, and EIO when the firmware reports a failure
 * in its answer.  Thread context only.
 */
int drv_rpi4_firmware_set_gpio(enum drv_rpi4_firmware_gpio line, int state);

/*
 * Reads whether an expander line is an input or an output.
 *
 * Changes nothing.  Returns EIO when the firmware reports a failure or a
 * direction it does not define.  Thread context only.
 */
int drv_rpi4_firmware_get_gpio_direction(enum drv_rpi4_firmware_gpio line, enum drv_rpi4_firmware_gpio_direction *direction);

#endif
