#ifndef KERN_HAL_ARM64_BSP_H
#define KERN_HAL_ARM64_BSP_H

#include "bsp-rpi4/fdt.h"

/*
 * The board's name in the HAL's boot messages and in the names of the
 * handoff objects the kernel reads ("rpi4.fdt", "rpi5.fdt").  The Raspberry
 * Pi 5 shares the Pi 4's board support and differs where HAL_BOARD_RPI5 says.
 */
#if defined(HAL_BOARD_RPI5)
#define ARM64_BOARD_TAG "RPI5"
#define ARM64_BOARD_HANDOFF "rpi5"
#else
#define ARM64_BOARD_TAG "RPI4"
#define ARM64_BOARD_HANDOFF "rpi4"
#endif

void rpi4_boot_set_info(const struct rpi4_fdt_info *info, uintptr_t fdt_phys);
void rpi4_boot_set_framebuffer(uint64_t phys,uint64_t size,uint32_t width,
	uint32_t height,uint32_t pitch,uint32_t format);
const struct rpi4_fdt_info *rpi4_boot_info(void);
uintptr_t rpi4_boot_fdt_phys(void);
const void *rpi4_kernel_handoff(void);
void prekern_bsp_cons_init(void);
void prekern_bsp_cons_irq_init(void);

#if defined(HAL_BOARD_RPI5)
void rpi5_psci_system_reset(void);
void rpi5_psci_system_off(void);
void rpi5_space_map_devices(void);
#endif

#endif
