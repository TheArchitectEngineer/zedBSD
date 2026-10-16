/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The harness of the reference display mode library (Linux v6.19
 * drivers/gpu/drm/amd/display/dc/dml, built on the host and never kept in
 * the tree).  It fills one pipe the way the reference driver fills it for
 * a plane (dc/dml/dcn20/dcn20_fpu.c, dcn20_populate_dml_pipes_from_context:
 * linear, 32 bits a pixel, unscaled), gives the library one clock state
 * (the board's clocks, and its copy, the extra state the library expects),
 * runs the voltage level's validation, then the watermarks and the
 * registers as dcn20_calculate_wm and dcn20_calculate_dlg_params do.
 * Built with the reference's include directories; the bounding box tables
 * (dcn2_0_ip, dcn2_0_soc) come from bounding-box.c, which the test's
 * script cuts out of dcn20_fpu.c.
 */

#include <string.h>

#include "display_mode_lib.h"
#include "display_mode_vba.h"
#include "dcn20/display_mode_vba_20v2.h"
#include "dcn20/display_rq_dlg_calc_20v2.h"

#include "oracle-api.h"

extern struct _vcs_dpi_ip_params_st dcn2_0_ip;
extern struct _vcs_dpi_soc_bounding_box_st dcn2_0_soc;

static struct display_mode_lib library;
static display_e2e_pipe_params_st pipes[1];

int
oracle_run(
	const struct oracle_input *input,
	struct oracle_output *output)
{
	display_pipe_source_params_st *source;
	display_pipe_dest_params_st *dest;
	display_dlg_regs_st dlg;
	display_ttu_regs_st ttu;
	display_rq_regs_st rq;
	struct _vcs_dpi_voltage_scaling_st state;
	unsigned level;
	unsigned combine;

	memset(output, 0, sizeof(*output));
	memset(&library, 0, sizeof(library));
	memset(pipes, 0, sizeof(pipes));

	/* The library with the Navi10 tables and one clock state, the board's (and the extra copy). */
	library.ip = dcn2_0_ip;
	library.soc = dcn2_0_soc;
	memset(&state, 0, sizeof(state));
	state.state = 0;
	state.dcfclk_mhz = input->dcfclk_mhz;
	state.fabricclk_mhz = input->fabricclk_mhz;
	state.dispclk_mhz = input->dispclk_mhz;
	state.dppclk_mhz = input->dppclk_mhz;
	state.phyclk_mhz = input->phyclk_mhz;
	state.socclk_mhz = input->socclk_mhz;
	state.dscclk_mhz = input->dispclk_mhz / 3.0;
	state.dram_speed_mts = input->dram_mts;
	memset(library.soc.clock_limits, 0, sizeof(library.soc.clock_limits));
	library.soc.clock_limits[0] = state;
	library.soc.clock_limits[1] = state;
	library.soc.clock_limits[1].state = 1;
	library.soc.num_states = 1;
	library.project = DML_PROJECT_NAVI10v2;
	library.funcs.validate = dml20v2_ModeSupportAndSystemConfigurationFull;
	library.funcs.recalculate = dml20v2_recalculate;
	library.funcs.rq_dlg_get_dlg_reg = dml20v2_rq_dlg_get_dlg_reg;
	library.funcs.rq_dlg_get_rq_reg = dml20v2_rq_dlg_get_rq_reg;

	/* The pipe, as the reference driver fills it for a plane. */
	source = &pipes[0].pipe.src;
	dest = &pipes[0].pipe.dest;
	pipes[0].clks_cfg.refclk_mhz = input->refclk_mhz;
	dest->synchronized_vblank_all_planes = true;
	dest->synchronize_timings = true;
	dest->hblank_start = input->htotal - input->h_front_porch;
	dest->hblank_end = dest->hblank_start - input->hactive;
	dest->vblank_start = input->vtotal - input->v_front_porch;
	dest->vblank_end = dest->vblank_start - input->vactive;
	dest->htotal = input->htotal;
	dest->vtotal = input->vtotal;
	dest->hactive = input->hactive;
	dest->vactive = input->vactive;
	dest->pixel_rate_mhz = input->pixel_mhz;
	dest->otg_inst = 0;
	dest->vtotal_min = input->vtotal;
	dest->vtotal_max = input->vtotal;
	dest->odm_combine = dm_odm_combine_mode_disabled;
	pipes[0].dout.dp_lanes = 4;
	pipes[0].dout.dp_rate = dm_dp_rate_na;
	pipes[0].dout.output_type = input->hdmi ? dm_hdmi : dm_dp;
	pipes[0].dout.output_format = dm_444;
	pipes[0].dout.output_bpp = 24;
	pipes[0].dout.dsc_input_bpc = 12;
	source->dcc = false;
	source->dcc_rate = 1;
	source->num_cursors = input->cursors;
	source->cur0_src_width = 256;
	source->cur0_bpp = dm_cur_32bit;
	source->source_scan = dm_horz;
	source->source_rotation = dm_rotation_0;
	source->viewport_width = input->hactive;
	source->viewport_height = input->vactive;
	source->viewport_width_max = input->hactive;
	source->viewport_height_max = input->vactive;
	source->surface_width_y = input->hactive;
	source->surface_height_y = input->vactive;
	source->data_pitch = input->pitch_pixels;
	source->macro_tile_size = dm_4k_tile;
	source->sw_mode = dm_sw_linear;
	source->source_format = dm_444_32;
	dest->recout_width = input->hactive;
	dest->recout_height = input->vactive;
	dest->full_recout_width = input->hactive;
	dest->full_recout_height = input->vactive;
	pipes[0].pipe.scale_ratio_depth.lb_depth = dm_lb_16;
	pipes[0].pipe.scale_ratio_depth.hscl_ratio = 1.0;
	pipes[0].pipe.scale_ratio_depth.vscl_ratio = 1.0;
	pipes[0].pipe.scale_ratio_depth.scl_enable = 0;
	pipes[0].pipe.scale_taps.htaps = 1;
	pipes[0].pipe.scale_taps.vtaps = 1;

	/*
	 * The voltage level: the validation of the one state (the self refresh
	 * choice left at its first value, as DCN 2.0 leaves it).  The driver
	 * shows the plane on one pipe, so the library is told never to combine
	 * two pipes for it (its default combines them whenever it can, and the
	 * reference driver then splits the plane).
	 */
	library.vba.WhenToDoMPCCombine = dm_mpc_never;
	level = dml_get_voltage_level(&library, pipes, 1);
	output->voltage_level = level;
	output->supported = level < library.soc.num_states;
	if (!output->supported)
		return 1;
	combine = library.vba.maxMpcComb;
	output->pipes = library.vba.NoOfDPP[level][combine][0];
	output->required_dispclk = library.vba.RequiredDISPCLK[level][combine];
	output->required_dppclk = library.vba.RequiredDPPCLK[level][combine][0];

	/* The watermarks at the board's clocks (dcn20_calculate_wm, the one state). */
	pipes[0].clks_cfg.dispclk_mhz = input->dispclk_mhz;
	pipes[0].clks_cfg.dppclk_mhz = input->dppclk_mhz;
	pipes[0].clks_cfg.voltage = level;
	pipes[0].clks_cfg.dcfclk_mhz = input->dcfclk_mhz;
	pipes[0].clks_cfg.socclk_mhz = input->socclk_mhz;
	output->wm_urgent = get_wm_urgent(&library, pipes, 1);
	output->wm_sr_enter_exit = get_wm_stutter_enter_exit(&library, pipes, 1);
	output->wm_sr_exit = get_wm_stutter_exit(&library, pipes, 1);
	output->wm_pstate = get_wm_dram_clock_change(&library, pipes, 1);
	output->wm_memory_trip = get_wm_memory_trip(&library, pipes, 1);
	output->dcfclk_deep_sleep = get_clk_dcf_deepsleep(&library, pipes, 1);

	/* The global sync and the registers (dcn20_calculate_dlg_params). */
	output->pstate_support = library.vba.DRAMClockChangeSupport[level][combine] != dm_dram_clock_change_unsupported;
	output->cstate_enable = library.vba.PrefetchMode[level][combine] != 2;
	dest->vstartup_start = get_vstartup(&library, pipes, 1, 0);
	dest->vupdate_offset = get_vupdate_offset(&library, pipes, 1, 0);
	dest->vupdate_width = get_vupdate_width(&library, pipes, 1, 0);
	dest->vready_offset = get_vready_offset(&library, pipes, 1, 0);
	output->vstartup = dest->vstartup_start;
	output->vupdate_offset = dest->vupdate_offset;
	output->vupdate_width = dest->vupdate_width;
	output->vready_offset = dest->vready_offset;
	memset(&dlg, 0, sizeof(dlg));
	memset(&ttu, 0, sizeof(ttu));
	memset(&rq, 0, sizeof(rq));
	library.funcs.rq_dlg_get_dlg_reg(&library, &dlg, &ttu, pipes, 1, 0, output->cstate_enable, output->pstate_support,
	    false, false, true);
	library.funcs.rq_dlg_get_rq_reg(&library, &rq, &pipes[0].pipe);

	/* The structures are all unsigned words; copied out as such. */
	output->dlg_count = sizeof(dlg) / sizeof(unsigned);
	output->ttu_count = sizeof(ttu) / sizeof(unsigned);
	output->rq_count = sizeof(rq) / sizeof(unsigned);
	if (output->dlg_count > 64U || output->ttu_count > 32U || output->rq_count > 32U)
		return 2;
	memcpy(output->dlg, &dlg, sizeof(dlg));
	memcpy(output->ttu, &ttu, sizeof(ttu));
	memcpy(output->rq, &rq, sizeof(rq));
	return 0;
}
