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

#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

/*
 * ---- the one encoder of the modeset object: hooks bound as intel_ddi_init() / intel_ddi_init_dp_connector()
 * bind them for a combo-PHY DDI on display version 12+ (reference: intel_ddi.c, the `encoder->... =` and
 * `dig_port->dp.... =` assignments; buffer translations: intel_ddi_buf_trans_init()).
 */
static struct parity_lcd_modeset *ddi_ms;

/*
 * intel_bios_hdmi_level_shift(): the VBT child's value for this port, as the parser read it (vbt/parity_vbt.h).
 * A value of 0 is a valid index; only a VBT without the field (< 0) makes intel_ddi_hdmi_level() fall back to
 * the buffer-translation table's own default entry.
 */
int parity_lcd_hdmi_level_shift(void)
{
	return ddi_ms != 0 ? ddi_ms->hdmi_level_shift : -1;
}

/* which screen's encoder the DDI callers are bound to right now (diagnostics) */
int parity_lcd_ms_bound_port(void)
{
	return ddi_ms != 0 ? (int)ddi_ms->dig_port.base.port : -1;
}

/* the objects of the bound screen, for a caller that walks the device's objects (the N1 readout) */
/*
 * The READOUT hooks of an encoder.  intel_ddi_init() binds these in the reference; they are static in this
 * unit, so the N1 readout asks for them here.  Without them the readout of an ACTIVE encoder calls a NULL
 * get_config (E-124 bare metal: amd64 fault v=14 rip=0 cr2=0, an instruction fetch at address 0).
 */
void parity_lcd_ms_bind_readout(struct intel_encoder *encoder)
{
	encoder->get_hw_state = intel_ddi_get_hw_state;
	encoder->get_config = intel_ddi_get_config;
	encoder->sync_state = intel_ddi_sync_state;
	encoder->get_power_domains = intel_ddi_get_power_domains;
}

struct intel_encoder *parity_lcd_ms_bound_encoder(void)
{
	return ddi_ms != 0 ? &ddi_ms->dig_port.base : 0;
}

struct intel_connector *parity_lcd_ms_bound_connector(void)
{
	return ddi_ms != 0 ? &ddi_ms->connector : 0;
}

void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms)
{
	struct intel_encoder *encoder = &ms->dig_port.base;
	struct intel_dp *intel_dp = &ms->dig_port.dp;

	ddi_ms = ms;
	/* intel_ddi_init(): encoder->power_domain = intel_display_power_ddi_lanes_domain() = POWER_DOMAIN_PORT_DDI_LANES_A + port */
	encoder->power_domain = POWER_DOMAIN_PORT_DDI_LANES_A + (int)encoder->port;
	encoder->base.index = 0;
	parity_lcd_only_encoder = &encoder->base;
	encoder->enable = intel_enable_ddi;
	encoder->pre_pll_enable = intel_ddi_pre_pll_enable;
	encoder->pre_enable = intel_ddi_pre_enable;
	encoder->disable = intel_disable_ddi;
	encoder->post_disable = intel_ddi_post_disable;
	encoder->post_pll_disable = intel_ddi_post_pll_disable;
	encoder->enable_clock = icl_ddi_combo_enable_clock;
	encoder->is_clock_enabled = icl_ddi_combo_is_clock_enabled;
	encoder->disable_clock = icl_ddi_combo_disable_clock;
	encoder->set_signal_levels = icl_combo_phy_set_signal_levels;
	parity_lcd_ms_bind_buf_trans(encoder);
	intel_dp->prepare_link_retrain = intel_ddi_prepare_link_retrain;
	intel_dp->set_link_train = intel_ddi_set_link_train;
	intel_dp->set_idle_link_train = intel_ddi_set_idle_link_train;
	intel_dp->voltage_max = intel_ddi_dp_voltage_max;
	intel_dp->preemph_max = intel_ddi_dp_preemph_max;
}

/*
 * The reference's intel_encoders_*() walk the atomic state's connectors and call the hook of each encoder
 * on the crtc.  Here there is exactly one encoder.  A hook sees the state the reference would hand it:
 * the new crtc state on the way up, the old one on the way down.
 */
#define DDI_MS_DISPATCH(hook, crtc_state_ptr) do { \
	struct intel_encoder *encoder__ = &ddi_ms->dig_port.base; \
	if (encoder__->hook) \
		encoder__->hook(state, encoder__, (crtc_state_ptr), &ddi_ms->conn_state); \
} while (0)

void intel_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(pre_pll_enable, state->crtc_state);
}

void intel_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(pre_enable, state->crtc_state);
}

void intel_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(enable, state->crtc_state);
}

void intel_encoders_disable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(disable, state->old_crtc_state);
}

void intel_encoders_post_disable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(post_disable, state->old_crtc_state);
}

void intel_encoders_post_pll_disable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(post_pll_disable, state->old_crtc_state);
}
