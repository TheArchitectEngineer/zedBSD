// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_hotplug_irq.c
 * (sha256 468732fe1d88edecb49ffe6a8c6b87ba99d844bf148b9aa236c17f1ffefdac79) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: icp_ddi_port_hotplug_long_detect, icp_tc_port_hotplug_long_detect, intel_get_hpd_pins, icp_irq_handler;
 *  - the includes are replaced by: hpd_compat.h;
 */

#include "hpd_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool icp_ddi_port_hotplug_long_detect(enum hpd_pin pin, u32 val);
static bool icp_tc_port_hotplug_long_detect(enum hpd_pin pin, u32 val);
static void intel_get_hpd_pins(struct drm_i915_private *dev_priv,
			       u32 *pin_mask, u32 *long_mask,
			       u32 hotplug_trigger, u32 dig_hotplug_reg,
			       const u32 hpd[HPD_NUM_PINS],
			       bool long_pulse_detect(enum hpd_pin pin, u32 val));

static bool icp_ddi_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
	case HPD_PORT_B:
	case HPD_PORT_C:
	case HPD_PORT_D:
		return val & SHOTPLUG_CTL_DDI_HPD_LONG_DETECT(pin);
	default:
		return false;
	}
}

static bool icp_tc_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return val & ICP_TC_HPD_LONG_DETECT(pin);
	default:
		return false;
	}
}

/*
 * Get a bit mask of pins that have triggered, and which ones may be long.
 * This can be called multiple times with the same masks to accumulate
 * hotplug detection results from several registers.
 *
 * Note that the caller is expected to zero out the masks initially.
 */
static void intel_get_hpd_pins(struct drm_i915_private *dev_priv,
			       u32 *pin_mask, u32 *long_mask,
			       u32 hotplug_trigger, u32 dig_hotplug_reg,
			       const u32 hpd[HPD_NUM_PINS],
			       bool long_pulse_detect(enum hpd_pin pin, u32 val))
{
	enum hpd_pin pin;

	BUILD_BUG_ON(BITS_PER_TYPE(*pin_mask) < HPD_NUM_PINS);

	for_each_hpd_pin(pin) {
		if ((hpd[pin] & hotplug_trigger) == 0)
			continue;

		*pin_mask |= BIT(pin);

		if (long_pulse_detect(pin, dig_hotplug_reg))
			*long_mask |= BIT(pin);
	}

	drm_dbg(&dev_priv->drm,
		"hotplug event received, stat 0x%08x, dig 0x%08x, pins 0x%08x, long 0x%08x\n",
		hotplug_trigger, dig_hotplug_reg, *pin_mask, *long_mask);
}

void icp_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	u32 ddi_hotplug_trigger = pch_iir & SDE_DDI_HOTPLUG_MASK_ICP;
	u32 tc_hotplug_trigger = pch_iir & SDE_TC_HOTPLUG_MASK_ICP;
	u32 pin_mask = 0, long_mask = 0;

	if (ddi_hotplug_trigger) {
		u32 dig_hotplug_reg;

		/* Locking due to DSI native GPIO sequences */
		spin_lock(&dev_priv->irq_lock);
		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, SHOTPLUG_CTL_DDI, 0, 0);
		spin_unlock(&dev_priv->irq_lock);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   ddi_hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   icp_ddi_port_hotplug_long_detect);
	}

	if (tc_hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, SHOTPLUG_CTL_TC, 0, 0);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   tc_hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   icp_tc_port_hotplug_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);

	if (pch_iir & SDE_GMBUS_ICP)
		intel_gmbus_irq_handler(dev_priv);
}

