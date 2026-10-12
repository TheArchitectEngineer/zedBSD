#ifndef KERN_HAL_ARM64_RPI4_GIC_H
#define KERN_HAL_ARM64_RPI4_GIC_H

#include <hal/types.h>

void rpi4_gic_init(void);
uint32_t rpi4_gic_ack(void);
void rpi4_gic_eoi(uint32_t value);
void rpi4_gic_mask(uint32_t intid);
void rpi4_gic_unmask(uint32_t intid);
int rpi4_gic_set_trigger(uint32_t intid, int trigger);
void rpi4_gic_init_cpu(void);
void rpi4_gic_send_sgi(uint32_t sgi, uint32_t target_interfaces);

/*
 * The software-generated interrupts the HAL uses between CPUs: one that
 * stops a CPU after a panic, and the kernel's notification.
 */
#define RPI4_SGI_STOP 0U
#define RPI4_SGI_NOTIFY 1U

#endif
