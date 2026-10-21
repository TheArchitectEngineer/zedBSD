#ifndef KERN_HAL_ARM64_DEFS_H
#define KERN_HAL_ARM64_DEFS_H

#define ARM64_PAGE_SIZE 4096
#define ARM64_DIRECT_BASE 0xffff000000000000
#define RPI4_PERIPHERAL_BASE 0xfe000000
#define RPI4_PL011_BASE 0xfe201000
#define RPI4_GPIO_BASE 0xfe200000

/*
 * The Raspberry Pi 5's peripherals the HAL reaches before it reads the
 * device tree: the debug UART (the three-pin connector between the HDMI
 * ports) and the always-on GPIO block that drives the activity LED.  Both
 * sit in the gigabyte from 0x10_4000_0000 that locore.S maps as device
 * memory.
 *
 * Values referenced: Raspberry Pi Linux rpi-6.12.y
 * arch/arm64/boot/dts/broadcom/bcm2712.dtsi (the soc node's ranges, uart10
 * and gio_aon).
 */
#define RPI5_DEBUG_UART_BASE 0x107d001000
#define RPI5_AON_GPIO_BASE 0x107d517c00

#endif
