/*
 * WS031 Linux-parity — zedBSD glue at the end of skl_watermark_port.c: the reference's skl_compute_wm() /
 * skl_compute_ddb() reduced to the one crtc of the modeset.  The per-crtc work is the reference's own
 * (skl_build_pipe_wm, the DBUF slice / MBUS / weight computation, skl_crtc_allocate_ddb,
 * skl_crtc_allocate_plane_ddb); what is reduced is the iteration over crtcs and the global-state locking.
 * Not covered here: intel_compute_sagv_mask() (the SAGV / bandwidth side of the commit).
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

struct parity_lcd_wm_ctx *parity_lcd_wm;

/*
 * The device's DBUF / MBUS state.  The reference keeps it in the global atomic state; here the screens
 * commit one after the other, so the state the last commit left is what the next one starts from.
 */
static struct intel_dbuf_state parity_lcd_dbuf_dev;
static int parity_lcd_dbuf_dev_valid;

int parity_lcd_dbuf_current(struct intel_dbuf_state *out)
{
	if (!parity_lcd_dbuf_dev_valid)
		return -1;
	*out = parity_lcd_dbuf_dev;
	return 0;
}

void parity_lcd_dbuf_publish(const struct intel_dbuf_state *now)
{
	parity_lcd_dbuf_dev = *now;
	parity_lcd_dbuf_dev_valid = 1;
}

/* a fresh device: the next prepare takes the DBUF state the caller read from the hardware */
void parity_lcd_dbuf_forget(void)
{
	memset(&parity_lcd_dbuf_dev, 0, sizeof(parity_lcd_dbuf_dev));
	parity_lcd_dbuf_dev_valid = 0;
}

int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms)
{
	struct intel_crtc_state *cs = &ms->crtc_state;
	struct intel_dbuf_state *new_dbuf = &ms->wm.new_dbuf;
	enum pipe pipe = ms->crtc.pipe;
	int ret;

	parity_lcd_wm = &ms->wm;
	ms->wm.crtc_state = cs;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = cs;
	/* what intel_plane_atomic_check() leaves for a visible plane: its data rates */
	cs->only_plane = &ms->plane;
	cs->only_plane_state = &ms->plane_state;
	parity_lcd_ms_plane_data_rates(ms);

	ret = skl_build_pipe_wm(&ms->state, &ms->crtc);
	if (ret)
		return ret;

	/* skl_compute_ddb() */
	ms->wm.old_dbuf.base.state = &ms->state;       /* a global state object knows its atomic state (-> the device) */
	*new_dbuf = ms->wm.old_dbuf;
	/* the whole configuration's pipes: this one and the others it lights (the reference's
	 * atomic state would hold every crtc) */
	new_dbuf->active_pipes = (u8)(ms->wm.old_dbuf.active_pipes | BIT(pipe) | ms->also_active_pipes);
	new_dbuf->joined_mbus = adlp_check_mbus_joined(new_dbuf->active_pipes);        /* HAS_MBUS_JOINING: ADL-P */
	new_dbuf->slices[pipe] = skl_compute_dbuf_slices(&ms->crtc, new_dbuf->active_pipes, new_dbuf->joined_mbus);
	new_dbuf->enabled_slices = intel_dbuf_enabled_slices(new_dbuf);
	new_dbuf->weight[pipe] = intel_crtc_ddb_weight(cs);
	/* a change of the slices / the MBUS joining serializes the global state (intel_atomic_serialize_global_state) */
	new_dbuf->base.changed = new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices ||
		new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus || new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes;
	ret = skl_crtc_allocate_ddb(&ms->state, &ms->crtc);
	if (ret)
		return ret;
	return skl_crtc_allocate_plane_ddb(&ms->state, &ms->crtc);
}

/* the same part of skl_compute_ddb() for the commit that turns the crtc off: no active pipe is left */
void parity_lcd_ms_wm_compute_off(struct parity_lcd_modeset *ms)
{
	struct intel_dbuf_state *new_dbuf = &ms->wm.new_dbuf;
	enum pipe pipe = ms->crtc.pipe;

	parity_lcd_wm = &ms->wm;
	*new_dbuf = ms->wm.old_dbuf;
	new_dbuf->active_pipes = ms->wm.old_dbuf.active_pipes & (u8)~BIT(pipe);
	new_dbuf->joined_mbus = adlp_check_mbus_joined(new_dbuf->active_pipes);
	new_dbuf->slices[pipe] = skl_compute_dbuf_slices(&ms->crtc, new_dbuf->active_pipes, new_dbuf->joined_mbus);
	new_dbuf->enabled_slices = intel_dbuf_enabled_slices(new_dbuf);
	new_dbuf->weight[pipe] = 0;
	memset(&new_dbuf->ddb[pipe], 0, sizeof(new_dbuf->ddb[pipe]));
	new_dbuf->base.changed = new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices ||
		new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus || new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes;
}

/*
 * The watermark readout of the reference is static in this unit and is reached through the device watermark
 * ops, which this path does not carry: the readout (N1) calls it through this boundary instead.
 */
void parity_lcd_wm_get_hw_state(struct drm_i915_private *i915)
{
	skl_wm_get_hw_state(i915);
}
