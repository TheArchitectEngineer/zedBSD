/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the riscv64 HAL asks of its board: the parts of a machine that the
 * device tree does not describe.
 */

#ifndef KERN_HAL_RISCV64_BSP_H
#define KERN_HAL_RISCV64_BSP_H

#include <hal/types.h>
#include "fdt.h"

void riscv64_board_init(const struct riscv64_fdt_info *info);
void riscv64_board_putc(int character);
void riscv64_board_describe(void);

#endif
