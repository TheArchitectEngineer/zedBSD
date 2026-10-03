/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_limits.h
 * (sha256 d8049c6ede79939d31462d7da810dd6afeeb9efc5a20f612e25450254c3140e1) by tools/port_lcd_calc.py: 
 * enum hpd_pin.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_HPD_PIN_ENUM_H
#define PARITY_HPD_PIN_ENUM_H

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

#endif /* PARITY_HPD_PIN_ENUM_H */
