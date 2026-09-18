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

/*
 * The whole crtc enable, driven by the reference's hsw_crtc_enable().  The atomic state is reduced to the
 * one new crtc state; the encoder (with its hooks bound as intel_ddi_init() binds them) comes from the DDI
 * glue, which also implements the three intel_encoders_*() dispatchers for this single encoder.
 * shared_dpll is non-NULL (a DP output always has one), so intel_enable_shared_dpll shows up as a step.
 */
struct intel_encoder *parity_ddi_seq_encoder(struct drm_i915_private *i915, int port, u32 saved_port_bits);

int parity_display_emit_crtc_enable(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int port, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp,
	int src_w, int src_h, u32 saved_port_bits)
{
	static struct drm_i915_private i915;
	static struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;
	struct intel_atomic_state state;
	static int some_dpll;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	memset(&state, 0, sizeof(state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.uapi.mode_changed = true;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.master_transcoder = INVALID_TRANSCODER;
	crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state.hsw_workaround_pipe = INVALID_PIPE;
	crtc_state.hw.adjusted_mode = *mode;
	drm_mode_set_crtcinfo(&crtc_state.hw.adjusted_mode, 0);
	crtc_state.pipe_src.x2 = src_w;
	crtc_state.pipe_src.y2 = src_h;
	crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state.port_clock = port_clock;
	crtc_state.lane_count = lanes;
	crtc_state.pipe_bpp = pipe_bpp;
	crtc_state.pixel_multiplier = 1;
	crtc_state.framestart_delay = 1;
	crtc_state.dp_m_n = *m_n;
	crtc_state.shared_dpll = &some_dpll;
	state.base.dev = &i915.drm;
	state.crtc_state = &crtc_state;
	if (parity_ddi_seq_encoder(&i915, port, saved_port_bits) == 0)
		return -EINVAL;

	hsw_crtc_enable(&state, &crtc);
	return crtc.active ? 0 : -EINVAL;
}
