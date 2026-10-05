/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Interrupt registration and control for drivers.
 *
 * Drivers reach the interrupt controller through the kernel, never through
 * the HAL. The acknowledgement token is opaque: a handler receives one and
 * gives it back to kern_irq_send_eoi() before it returns.
 */

#ifndef KERN_IRQ_H
#define KERN_IRQ_H

#include <stdbool.h>
#include <stdint.h>

/* One pending acknowledgement, owned by the handler it was passed to. */
typedef uintptr_t kern_irq_ack_t;

#define KERN_IRQ_ACK_NONE ((kern_irq_ack_t)0)

/*
 * An interrupt handler.
 *
 * It runs in interrupt context, so it must not sleep, and it must call
 * kern_irq_send_eoi() with its acknowledgement before returning.
 */
typedef void (*kern_irq_handler_t)(int irq, kern_irq_ack_t acknowledge,
				   void *argument);

/*
 * Install or remove a handler for one numbered interrupt.
 *
 * Removal requires the same handler and argument that were registered.
 * Both report 0 on success.
 */
int kern_irq_register(int irq, kern_irq_handler_t handler, void *argument);
int kern_irq_unregister(int irq, kern_irq_handler_t handler, void *argument);

/*
 * Allocate one message-signalled interrupt for a device.
 *
 * source is a canonical bus identity such as "PCI 0000:00:1f.2". The output
 * values are published only after the handler is fully installed.
 */
int kern_irq_register_msi(const char *source, kern_irq_handler_t handler,
			  void *argument, int *mapped_irq,
			  uint64_t *mapped_address, uint32_t *mapped_event);
int kern_irq_unregister_msi(int mapped_irq);

/*
 * The trigger mode and polarity of a numbered interrupt
 * (kern_irq_set_mode): edge or level, active high or low.
 */
#define KERN_IRQ_TRIGGER_EDGE	0U
#define KERN_IRQ_TRIGGER_LEVEL	1U
#define KERN_IRQ_POLARITY_HIGH	0U
#define KERN_IRQ_POLARITY_LOW	1U

/*
 * Sets the trigger mode and polarity of one numbered interrupt, as a
 * device's ACPI resource describes it (the line is masked meanwhile and
 * its mask state kept).  Returns 0, EINVAL, EBUSY while the line is being
 * delivered, or ENOTSUP when the interrupt controller cannot represent it.
 */
int kern_irq_set_mode(int irq, unsigned trigger, unsigned polarity);

/*
 * Arms or disarms an interrupt as a wake source of a system sleep
 * (ws052-p006, kern_irq_set_wake): an armed interrupt keeps being
 * delivered while the others are held, and its handler then runs while
 * its device may be suspended, so it must only record what happened (as
 * the ACPI SCI's handler does), not touch a device in D3.
 */
int kern_irq_set_wake(int irq, int enable);

/* Stop and resume delivery of one numbered interrupt. */
void kern_irq_mask(int irq);
void kern_irq_unmask(int irq);

/* Retire one acknowledgement. A handler must do this before it returns. */
void kern_irq_send_eoi(kern_irq_ack_t acknowledge);

/*
 * Disable and restore interrupt delivery on the current processor.
 *
 * kern_irq_disable() reports whether delivery was enabled beforehand, so
 * the caller can restore exactly that state.
 */
bool kern_irq_disable(void);
void kern_irq_enable(void);

#endif
