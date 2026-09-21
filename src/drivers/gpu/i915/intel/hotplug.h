/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 * Copyright © 2019 Intel Corporation
 */

/*
 * Copyright © 2006-2019 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The hot-plug definitions of the Linux display headers, for the hotplug
 * environment: enum hpd_pin, for_each_hpd_pin(), enum intel_hotplug_state,
 * the hot-plug detect and south display interrupt registers of i915_reg.h,
 * and the GMBUS registers and pins.
 *
 * The HPD pins (Linux intel_display_limits.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_limits.h
 * (sha256 d8049c6ede79939d31462d7da810dd6afeeb9efc5a20f612e25450254c3140e1) by tools/port_lcd_calc.py:
 * enum hpd_pin.
 * The notice above is the source file's own.
 *
 * for_each_hpd_pin() (Linux intel_display.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display.h
 * (sha256 dde8fd52907f640d35a5a490a85103a9430f1144d2a3b78c6a24041c884996e0) by tools/port_lcd_calc.py:
 * for_each_hpd_pin.
 * The notice above is the source file's own.
 *
 * The hot-plug states (Linux intel_display_types.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_types.h
 * (sha256 647773fcee0c549791a50b9c6a4cde175c11005497689e9d6d4bbd2db8d38db4) by tools/port_lcd_calc.py:
 * enum intel_hotplug_state.
 * The notice above is the source file's own.
 *
 * The hot-plug and south display interrupt registers (Linux i915_reg.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/i915_reg.h
 * (sha256 82d048e09a8472af07e7d3e9db146cd849988a10d8836e64116ff26a6e6b3ed3) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The GMBUS registers (Linux intel_gmbus_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_gmbus_regs.h
 * (sha256 ce5f6978df207e8322d0527a4604fef8426f7686d7265018368b19aa0d6823ba) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The GMBUS pins (Linux intel_gmbus.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_gmbus.h
 * (sha256 138bdbd3271a52cf07136da53ef992f7664a8123e182071ff810f8a6347af3ea) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_HOTPLUG_H
#define DRIVERS_GPU_I915_INTEL_HOTPLUG_H

/* The HPD pins (Linux intel_display_limits.h). */

enum hpd_pin {
	HPD_NONE = 0,
	HPD_TV = HPD_NONE,     /* TV is known to be unreliable */
	HPD_CRT,
	HPD_SDVO_B,
	HPD_SDVO_C,
	HPD_PORT_A,
	HPD_PORT_B,
	HPD_PORT_C,
	HPD_PORT_D,
	HPD_PORT_E,
	HPD_PORT_TC1,
	HPD_PORT_TC2,
	HPD_PORT_TC3,
	HPD_PORT_TC4,
	HPD_PORT_TC5,
	HPD_PORT_TC6,

	HPD_NUM_PINS
};

/* for_each_hpd_pin() (Linux intel_display.h). */

#define for_each_hpd_pin(__pin) \
	for ((__pin) = (HPD_NONE + 1); (__pin) < HPD_NUM_PINS; (__pin)++)

/* The hot-plug states (Linux intel_display_types.h). */

enum intel_hotplug_state {
	INTEL_HOTPLUG_UNCHANGED,
	INTEL_HOTPLUG_CHANGED,
	INTEL_HOTPLUG_RETRY,
};

/* The hot-plug and south display interrupt registers (Linux i915_reg.h). */

#define _HPD_PIN_DDI(hpd_pin)	((hpd_pin) - HPD_PORT_A)
#define _HPD_PIN_TC(hpd_pin)	((hpd_pin) - HPD_PORT_TC1)
#define SDE_GMBUS_ICP			(1 << 23)
#define SDE_TC_HOTPLUG_ICP(hpd_pin)	REG_BIT(24 + _HPD_PIN_TC(hpd_pin))
#define SDE_DDI_HOTPLUG_ICP(hpd_pin)	REG_BIT(16 + _HPD_PIN_DDI(hpd_pin))
#define SDE_DDI_HOTPLUG_MASK_ICP	(SDE_DDI_HOTPLUG_ICP(HPD_PORT_D) | \
					 SDE_DDI_HOTPLUG_ICP(HPD_PORT_C) | \
					 SDE_DDI_HOTPLUG_ICP(HPD_PORT_B) | \
					 SDE_DDI_HOTPLUG_ICP(HPD_PORT_A))
#define SDE_TC_HOTPLUG_MASK_ICP		(SDE_TC_HOTPLUG_ICP(HPD_PORT_TC6) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC5) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC4) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC3) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC2) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC1))
#define SDEISR  _MMIO(0xc4000)
#define SHOTPLUG_CTL_DDI				_MMIO(0xc4030)
#define   SHOTPLUG_CTL_DDI_HPD_LONG_DETECT(hpd_pin)		(0x2 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define SHOTPLUG_CTL_TC				_MMIO(0xc4034)
#define   ICP_TC_HPD_LONG_DETECT(hpd_pin)	(2 << (_HPD_PIN_TC(hpd_pin) * 4))

/* The GMBUS registers (Linux intel_gmbus_regs.h). */

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

/* The GMBUS pins (Linux intel_gmbus.h). */

#define GMBUS_PIN_2_BXT		2
#define GMBUS_NUM_PINS	15 /* including 0 */

#endif /* DRIVERS_GPU_I915_INTEL_HOTPLUG_H */
