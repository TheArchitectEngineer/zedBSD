#!/usr/bin/env python3
"""WS031 E-114: the resident eDP executes the panel-power and DPCD operations of the LCD modeset;
the sink model tells its owner about native DPCD writes (link training).  usage: <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
D = "src/drivers/gpu/i915/parity/dp/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

h = load(D + "parity_edp.h")
h = rep(h, "#endif /* PARITY_EDP_H */", """/*
 * The modeset side (parity/lcd) drives the panel through the live eDP -- the reference's own
 * intel_pps_*() and drm_dp_dpcd_*() run here, with their locks, waits and references.
 * `op` = enum parity_lcd_panel_op (parity/lcd/parity_lcd_ops.h).  0, or -EINVAL without a live eDP /
 * for an unknown op.  The PPS functions return nothing: their effect is read back by the caller
 * (parity_edp_snapshot, PP_STATUS).
 */
int parity_edp_panel_op(int op);
long parity_edp_dpcd_write(unsigned offset, const uint8_t *buf, size_t size);
/* drm_dp_read_dpcd_caps() on the live AUX channel: 0, or a negative errno */
int parity_edp_read_dpcd_caps(uint8_t dpcd[15]);

#endif /* PARITY_EDP_H */""")
save(D + "parity_edp.h", h)

c = load(D + "parity_edp.c")
c = c.rstrip(NL) + NL + """
long parity_edp_dpcd_write(unsigned offset, const uint8_t *buf, size_t size)
{
	if (!edp.live)
		return -EINVAL;
	return drm_dp_dpcd_write(&edp.dig_port.dp.aux, offset, (void *)buf, size);
}

int parity_edp_read_dpcd_caps(uint8_t dpcd[15])
{
	if (!edp.live)
		return -EINVAL;
	return drm_dp_read_dpcd_caps(&edp.dig_port.dp.aux, dpcd);
}

/* enum parity_lcd_panel_op, kept numerically in step with parity/lcd/parity_lcd_ops.h (checked by the tests) */
int parity_edp_panel_op(int op)
{
	struct intel_dp *intel_dp = &edp.dig_port.dp;

	if (!edp.live)
		return -EINVAL;
	switch (op) {
	case 0: intel_pps_on(intel_dp); return 0;
	case 1: intel_pps_off(intel_dp); return 0;
	case 2: intel_pps_vdd_on(intel_dp); return 0;
	case 3: intel_pps_vdd_off_sync(intel_dp); return 0;
	case 4: intel_pps_backlight_on(intel_dp); return 0;
	case 5: intel_pps_backlight_off(intel_dp); return 0;
	default: return -EINVAL;
	}
}
"""
save(D + "parity_edp.c", c)

f = load(D + "dp_fake_hw.h")
f = rep(f, "	unsigned wait_timeouts;" + NL + "};", "	unsigned wait_timeouts;" + NL +
        "	/* told about every native DPCD write after it is stored (the sink's link-training behaviour lives with the owner) */" + NL +
        "	void (*on_dpcd_write)(void *ctx, unsigned addr, unsigned len);" + NL +
        "	void *on_dpcd_write_ctx;" + NL + "};")
save(D + "dp_fake_hw.h", f)
k = load(D + "dp_fake_hw.c")
k = rep(k, """					hw->dpcd[addr + i] = msg[4u + i];
""", """					hw->dpcd[addr + i] = msg[4u + i];
			if (hw->on_dpcd_write != 0)
				hw->on_dpcd_write(hw->on_dpcd_write_ctx, addr, len);
""")
save(D + "dp_fake_hw.c", k)
print("done")
