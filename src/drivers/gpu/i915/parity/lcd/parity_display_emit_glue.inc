/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_display_port.c.
 *
 * Builds the crtc state the reference's writer functions read, then calls them:
 *   intel_cpu_transcoder_set_m1_n1()   (hsw_configure_cpu_transcoder does M/N first ...)
 *   intel_set_transcoder_timings()     (... then the timings)
 *   intel_set_pipe_src_size()
 * The mode's crtc_* fields come from the reference's drm_mode_set_crtcinfo() with
 * adjust_flags 0 -- a progressive, non-doubled panel mode -- as intel_crtc_compute_config
 * leaves them.  The register writes go wherever `emit` sends them.
 */
int parity_display_emit_transcoder(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int src_w, int src_h)
{
	static struct drm_i915_private i915;
	struct intel_crtc crtc;
	struct intel_crtc_state crtc_state;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.hw.adjusted_mode = *mode;
	drm_mode_set_crtcinfo(&crtc_state.hw.adjusted_mode, 0);
	crtc_state.pipe_src.x1 = 0;
	crtc_state.pipe_src.y1 = 0;
	crtc_state.pipe_src.x2 = src_w;
	crtc_state.pipe_src.y2 = src_h;

	intel_cpu_transcoder_set_m1_n1(&crtc, (enum transcoder)cpu_transcoder, m_n);
	intel_set_transcoder_timings(&crtc_state);
	intel_set_pipe_src_size(&crtc_state);
	return 0;
}

/*
 * The same state handed to the reference's OWN caller, hsw_configure_cpu_transcoder(): M/N, timings,
 * VRR words, TRANS_MULT, frame start delay, TRANSCONF -- the order between them is the reference's.
 * State as intel_crtc_compute_config / the DP encoder's compute_config leave it for an SST panel:
 * a full modeset (mode_changed), pixel_multiplier 1, framestart_delay 1, VRR not in use (flipline 0),
 * RGB output, no PCH encoder.
 */
int parity_display_emit_cpu_transcoder(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp)
{
	static struct drm_i915_private i915;
	struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.uapi.mode_changed = true;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.master_transcoder = INVALID_TRANSCODER;
	crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state.hw.adjusted_mode = *mode;
	drm_mode_set_crtcinfo(&crtc_state.hw.adjusted_mode, 0);
	crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state.port_clock = port_clock;
	crtc_state.lane_count = lanes;
	crtc_state.pipe_bpp = pipe_bpp;
	crtc_state.pixel_multiplier = 1;
	crtc_state.framestart_delay = 1;
	crtc_state.dp_m_n = *m_n;

	hsw_configure_cpu_transcoder(&crtc_state);
	return 0;
}

#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

/*
 * The crtc enable / disable of the one modeset object, driven by the reference's hsw_crtc_enable() /
 * hsw_crtc_disable().  The atomic state is reduced to the one crtc state: it is the NEW state of the
 * enable and the OLD state of the disable.
 */
struct drm_i915_private *parity_lcd_cur_i915;
struct drm_encoder *parity_lcd_only_encoder;

void parity_lcd_ms_crtc_enable(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;
	ms->state.old_crtc_state = 0;
	hsw_crtc_enable(&ms->state, &ms->crtc);
}

void parity_lcd_ms_crtc_disable(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = 0;
	ms->state.old_crtc_state = &ms->crtc_state;
	hsw_crtc_disable(&ms->state, &ms->crtc);
	ms->crtc.active = false;                /* intel_old_crtc_state_disables() clears it after the hook */
}
