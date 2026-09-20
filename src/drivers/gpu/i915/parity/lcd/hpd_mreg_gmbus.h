/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_gmbus_regs.h
 * (sha256 ce5f6978df207e8322d0527a4604fef8426f7686d7265018368b19aa0d6823ba) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_HPD_MREG_GMBUS_H
#define PARITY_HPD_MREG_GMBUS_H

#define GMBUS_MMIO_BASE(__i915) ((__i915)->display.gmbus.mmio_base)
#define GMBUS0(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5100)
#define   GMBUS_RATE_100KHZ		(0 << 8)
#define   GMBUS_BYTE_CNT_OVERRIDE	(1 << 6)
#define GMBUS1(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5104)
#define   GMBUS_SW_CLR_INT		(1 << 31)
#define   GMBUS_SW_RDY			(1 << 30)
#define   GMBUS_CYCLE_WAIT		(1 << 25)
#define   GMBUS_CYCLE_INDEX		(2 << 25)
#define   GMBUS_CYCLE_STOP		(4 << 25)
#define   GMBUS_BYTE_COUNT_SHIFT	16
#define   GMBUS_BYTE_COUNT_MAX		256U
#define   GEN9_GMBUS_BYTE_COUNT_MAX	511U
#define   GMBUS_SLAVE_INDEX_SHIFT	8
#define   GMBUS_SLAVE_ADDR_SHIFT	1
#define   GMBUS_SLAVE_READ		(1 << 0)
#define   GMBUS_SLAVE_WRITE		(0 << 0)
#define GMBUS2(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5108)
#define   GMBUS_HW_WAIT_PHASE		(1 << 14)
#define   GMBUS_HW_RDY			(1 << 11)
#define   GMBUS_SATOER			(1 << 10)
#define   GMBUS_ACTIVE			(1 << 9)
#define GMBUS3(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x510c)
#define GMBUS4(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5110)
#define   GMBUS_IDLE_EN			(1 << 2)
#define   GMBUS_HW_WAIT_EN		(1 << 1)
#define   GMBUS_HW_RDY_EN		(1 << 0)
#define GMBUS5(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5120)
#define   GMBUS_2BYTE_INDEX_EN		(1 << 31)

#endif /* PARITY_HPD_MREG_GMBUS_H */
