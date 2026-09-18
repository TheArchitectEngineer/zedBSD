/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_ddi_port.c.
 *
 * Builds the encoder / digital-port / crtc state the kept intel_ddi.c functions read and calls them:
 *   intel_ddi_init_dp_buf_reg()          -> intel_dp->DP, the DDI_BUF_CTL value (returned, not written)
 *   intel_ddi_set_dp_msa()               -> TRANS_MSA_MISC
 *   intel_ddi_enable_transcoder_func()   -> TRANS_DDI_FUNC_CTL2, TRANS_DDI_FUNC_CTL
 * saved_port_bits is the caller's: intel_ddi_init() takes it from the DDI_BUF_CTL readout masked with
 * DDI_BUF_PORT_REVERSAL (display version >= 11) plus the VBT's lane-reversal flag.
 */
int parity_ddi_emit(struct parity_lcd_emit *emit, const struct drm_display_mode *mode, int port, int pipe,
	int cpu_transcoder, int port_clock, int lanes, int pipe_bpp, u32 saved_port_bits, u32 *ddi_buf_ctl_value)
{
	static struct drm_i915_private i915;
	struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;
	struct intel_digital_port dig_port;
	struct drm_connector_state conn_state;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	memset(&dig_port, 0, sizeof(dig_port));
	memset(&conn_state, 0, sizeof(conn_state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.uapi.mode_changed = true;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.master_transcoder = INVALID_TRANSCODER;
	crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state.hw.adjusted_mode = *mode;
	crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state.port_clock = port_clock;
	crtc_state.lane_count = lanes;
	crtc_state.pipe_bpp = pipe_bpp;
	dig_port.base.base.dev = &i915.drm;
	dig_port.base.port = (enum port)port;
	dig_port.saved_port_bits = saved_port_bits;
	conn_state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;

	intel_ddi_init_dp_buf_reg(&dig_port.base, &crtc_state);
	*ddi_buf_ctl_value = dig_port.dp.DP;
	intel_ddi_set_dp_msa(&crtc_state, &conn_state);
	intel_ddi_enable_transcoder_func(&dig_port.base, &crtc_state);
	return 0;
}

/* ---- the enable sequence: one encoder, hooks bound as intel_ddi_init() binds them ---- */
static struct intel_digital_port seq_dig_port;
static struct drm_connector_state seq_conn_state;

struct drm_i915_private *parity_lcd_seq_dp_i915(const struct intel_dp *intel_dp)
{
	return to_i915(container_of(intel_dp, struct intel_digital_port, dp)->base.base.dev);
}

/*
 * encoder->set_signal_levels: intel_ddi_init() picks icl_combo_phy_set_signal_levels for a combo PHY on
 * display version >= 12.  Not ported: a named step.
 */
static void seq_set_signal_levels(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state)
{
	PARITY_LCD_STEP(to_i915(encoder->base.dev), "encoder->set_signal_levels (icl_combo_phy_set_signal_levels)");
}

struct intel_encoder *parity_ddi_seq_encoder(struct drm_i915_private *i915, int port, u32 saved_port_bits)
{
	if (port < 0 || port > 1)               /* combo PHY ports only */
		return 0;
	memset(&seq_dig_port, 0, sizeof(seq_dig_port));
	memset(&seq_conn_state, 0, sizeof(seq_conn_state));
	seq_dig_port.base.base.dev = &i915->drm;
	seq_dig_port.base.port = (enum port)port;
	seq_dig_port.saved_port_bits = saved_port_bits;
	seq_dig_port.base.enable = intel_enable_ddi;
	seq_dig_port.base.pre_pll_enable = intel_ddi_pre_pll_enable;
	seq_dig_port.base.pre_enable = intel_ddi_pre_enable;
	seq_dig_port.base.set_signal_levels = seq_set_signal_levels;
	seq_conn_state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	return &seq_dig_port.base;
}

/*
 * The reference's intel_encoders_*() walk the atomic state's connectors and call the hook of each encoder
 * on this crtc.  Here there is exactly one encoder; the dispatcher itself is recorded as a step so the
 * list shows where hsw_crtc_enable() hands over to the encoder.
 */
void intel_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	PARITY_LCD_STEP(to_i915(crtc->base.dev), "> intel_encoders_pre_pll_enable");
	if (seq_dig_port.base.pre_pll_enable)
		seq_dig_port.base.pre_pll_enable(state, &seq_dig_port.base, state->crtc_state, &seq_conn_state);
}

void intel_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	PARITY_LCD_STEP(to_i915(crtc->base.dev), "> intel_encoders_pre_enable");
	if (seq_dig_port.base.pre_enable)
		seq_dig_port.base.pre_enable(state, &seq_dig_port.base, state->crtc_state, &seq_conn_state);
}

void intel_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	PARITY_LCD_STEP(to_i915(crtc->base.dev), "> intel_encoders_enable");
	if (seq_dig_port.base.enable)
		seq_dig_port.base.enable(state, &seq_dig_port.base, state->crtc_state, &seq_conn_state);
}
