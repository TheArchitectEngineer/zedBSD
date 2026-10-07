/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The plain words between host-dkl-test.c and its Linux reference
 * (host-dkl-linux.c).
 */

#ifndef PLAN_WS051_TESTS_HOST_DKL_LINUX_H
#define PLAN_WS051_TESTS_HOST_DKL_LINUX_H

#include <stdint.h>

/* The eight words of a DKL PLL, as the PLL state and the debugfs dump name them. */
struct host_dkl_words {
	uint32_t refclkin_ctl;
	uint32_t clktop2_coreclkctl1;
	uint32_t clktop2_hsclkctl;
	uint32_t div0;
	uint32_t div1;
	uint32_t ssc;
	uint32_t bias;
	uint32_t tdc_coldst_bias;
};

int host_dkl_linux_calc(int port_clock, int is_hdmi, int ref_nssc, struct host_dkl_words *words);
int host_dkl_linux_freq(int ref_nssc, const struct host_dkl_words *words);

#endif
