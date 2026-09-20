#!/usr/bin/env python3
"""WS031 E-115 round 17: the model checks the DDB / watermark of an armed plane; tests feed the real WM inputs.
usage: round17.py <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

h = load(L + "lcd_fake_hw.h")
h = rep(h, "	int fault_power_get;", "	uint32_t fault_drop_write_reg;          /* writes to this register are lost (a test of the MODEL: does it notice?) */" + NL + "	int fault_power_get;")
h = rep(h, "	unsigned pattern_mismatch;", "	unsigned pattern_mismatch;" + NL +
        "	unsigned plane_armed_without_ddb;       /* armed with PLANE_BUF_CFG empty / outside the DBUF, or watermark level 0 disabled */" + NL +
        "	uint32_t dbuf_size;                     /* DDB blocks of the platform (for the range check) */")
save(L + "lcd_fake_hw.h", h)

c = load(L + "lcd_fake_hw.c")
c = rep(c, "#define  PLANE_CTL_ENABLE     (1u << 31)", "#define  PLANE_CTL_ENABLE     (1u << 31)" + NL +
        "#define REG_PLANE_WM0(p)      (0x70240u + 0x1000u * (unsigned)(p))" + NL +
        "#define REG_PLANE_BUF_CFG(p)  (0x7027cu + 0x1000u * (unsigned)(p))")
c = rep(c, "	uint32_t *v = slot(hw, reg, 1), old = *v;" + NL, "	uint32_t *v, old;" + NL + NL +
        "	if (hw->fault_drop_write_reg != 0u && reg == hw->fault_drop_write_reg)" + NL + "		return;" + NL +
        "	v = slot(hw, reg, 1);" + NL + "	old = *v;" + NL)
c = rep(c, "			if (hw->pipe_on_since_us == 0u)" + NL + "				hw->plane_armed_without_pipe++;" + NL,
        "			if (hw->pipe_on_since_us == 0u)" + NL + "				hw->plane_armed_without_pipe++;" + NL +
        "			{" + NL +
        "				/* PLANE_BUF_CFG: start bits 11:0, end (last block) bits 27:16 -- 12-bit fields on display version 13 */" + NL +
        "				uint32_t cfg = lcd_fake_reg(hw, REG_PLANE_BUF_CFG(hw->pipe));" + NL +
        "				uint32_t start = cfg & 0xfffu, last = (cfg >> 16) & 0xfffu;" + NL + NL +
        "				if (cfg == 0u || last < start || (hw->dbuf_size != 0u && last >= hw->dbuf_size) ||" + NL +
        "				    !(lcd_fake_reg(hw, REG_PLANE_WM0(hw->pipe)) & (1u << 31)))" + NL +
        "					hw->plane_armed_without_ddb++;" + NL +
        "			}" + NL)
c = rep(c, "		hw->pll_disabled_with_pipe_on + hw->training_without_panel_power + hw->pattern_mismatch +",
        "		hw->pll_disabled_with_pipe_on + hw->training_without_panel_power + hw->pattern_mismatch + hw->plane_armed_without_ddb +")
save(L + "lcd_fake_hw.c", c)

WM = ("	/* watermark inputs as the target's normal initialisation logs them (P5a: levels 6, latency 3/54/83/102/147/147/144/144," + NL +
      "	 * SAGV block time 35 us; XE_LPD: DBUF 4096 blocks in 4 slices, IPC; slice 1 enabled after power-domain init) */" + NL +
      "	{ static const uint16_t lat[8] = { 3, 54, 83, 102, 147, 147, 144, 144 }; memcpy(c.wm_latency, lat, sizeof(lat)); }" + NL +
      "	c.wm_num_levels = 6; c.wm_ipc_enabled = 1; c.sagv_block_time_us = 35; c.dbuf_size = 4096; c.dbuf_slice_mask = 0x0f; c.dbuf_enabled_slices = 0x01;" + NL)
t = load("plan/ws031/tests/lcd-modeset-host-test.c")
t = rep(t, "	c.dmc_fw_mask = 1u << 1;", WM + "	c.dmc_fw_mask = 1u << 1;")
t = rep(t, "	lcd_fake_init(&lcd, &dpf, 0, 0, 0, st.mode.vtotal);", "	lcd_fake_init(&lcd, &dpf, 0, 0, 0, st.mode.vtotal);" + NL + "	lcd.dbuf_size = 4096;")
t = rep(t, '	CHECK(lcd_fake_reg(&lcd, 0x70188) == 0x78u && lcd_fake_reg(&lcd, 0x70190) == 0x0437077fu, "A: PLANE_STRIDE / PLANE_SIZE = Linux\'s dump");',
        '	CHECK(lcd_fake_reg(&lcd, 0x70188) == 0x78u && lcd_fake_reg(&lcd, 0x70190) == 0x0437077fu, "A: PLANE_STRIDE / PLANE_SIZE = Linux\'s dump");' + NL +
        '	printf("  A: wm rc=%d ddb=[%u,%u) wm0 en=%d blocks=%u lines=%u slices=0x%x mbus_joined=%d | PLANE_WM_1_A_0=0x%08x BUF_CFG=0x%08x WM_TRANS=0x%08x WM_SAGV=0x%08x\\n",' + NL +
        '	       s.wm_rc, s.ddb_start, s.ddb_end, s.wm0_enable, s.wm0_blocks, s.wm0_lines, s.dbuf_slices_wanted, s.mbus_joined,' + NL +
        '	       lcd_fake_reg(&lcd, 0x70240), lcd_fake_reg(&lcd, 0x7027c), lcd_fake_reg(&lcd, 0x70268), lcd_fake_reg(&lcd, 0x70258));' + NL +
        '	CHECK(s.wm_rc == 0 && s.ddb_start == 0 && s.ddb_end == 4060 && lcd_fake_reg(&lcd, 0x7027c) == 0x0fdb0000u,' + NL +
        '	      "A: DDB [0, 4060) of the joined 4096-block DBUF (the rest is the cursor\'s reserve); PLANE_BUF_CFG = end - 1 encoded = Linux\'s dump 0x0fdb0000");' + NL +
        '	CHECK(s.wm0_enable == 1 && lcd_fake_reg(&lcd, 0x70240) == 0x80004010u,' + NL +
        '	      "A: watermark level 0 from the target\'s latencies = Linux\'s dump 0x80004010 (enable, 1 line, 16 blocks)");' + NL +
        '	CHECK(lcd.plane_armed_without_ddb == 0, "A: the plane was armed with a valid DDB range and an enabled level-0 watermark");')
t = rep(t, "	/* ================= C. trouble after the plane was armed ================= */",
        """	/* ================= the model really checks the DDB: lose the PLANE_BUF_CFG write ================= */
	bring_up();
	lcd.fault_drop_write_reg = 0x7027c;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_enable() : -99;
	rc = rc == 0 ? parity_lcd_modeset_plane_update() : rc;
	CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_armed_without_ddb == 1 && lcd_fake_violations(&lcd) == 1,
	      "model self-test: with the PLANE_BUF_CFG write lost, arming the plane is counted as a violation");
	(void)parity_lcd_modeset_plane_disable();
	parity_lcd_modeset_plane_released();
	(void)parity_lcd_modeset_disable();
	(void)parity_edp_end(&res);

	/* ================= C. trouble after the plane was armed ================= */""")
save("plan/ws031/tests/lcd-modeset-host-test.c", t)

k = load(L + "lcd_modeset_ktest.c")
k = rep(k, "	cfg.dmc_fw_mask = 1u << 1;", "	{" + NL + "		static const uint16_t lat[8] = { 3, 54, 83, 102, 147, 147, 144, 144 };" + NL + NL +
        "		memcpy(cfg.wm_latency, lat, sizeof(lat));" + NL + "	}" + NL +
        "	cfg.wm_num_levels = 6; cfg.wm_ipc_enabled = 1; cfg.sagv_block_time_us = 35;" + NL +
        "	cfg.dbuf_size = 4096u; cfg.dbuf_slice_mask = 0x0f; cfg.dbuf_enabled_slices = 0x01;" + NL +
        "	lcd.dbuf_size = 4096u;" + NL + "	cfg.dmc_fw_mask = 1u << 1;")
k = rep(k, "		lcd.plane_surf_at_arm == 0xfdfc0000u && f1 >= f0 + 29u,",
        "		lcd.plane_surf_at_arm == 0xfdfc0000u && f1 >= f0 + 29u && lcd.plane_armed_without_ddb == 0u &&" + NL +
        "		lcd_fake_reg(&lcd, 0x7027cu) == 0x0fdb0000u && lcd_fake_reg(&lcd, 0x70240u) == 0x80004010u,")
save(L + "lcd_modeset_ktest.c", k)
print("done")
