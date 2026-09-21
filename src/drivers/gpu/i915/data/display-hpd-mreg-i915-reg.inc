/* Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/i915_reg.h
 * (sha256 82d048e09a8472af07e7d3e9db146cd849988a10d8836e64116ff26a6e6b3ed3) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/hpd_mreg_i915_reg.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_HPD_MREG_I915_REG_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_HPD_MREG_I915_REG_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_HPD_MREG_I915_REG_INC

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

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_HPD_MREG_I915_REG_INC */
