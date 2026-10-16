<!-- awesome-plan project=zedbsd record=ws245-p045 -->

# ws245-p045: display 11（Ice Lake）

Status: planned（2026-10-17 P6: q1236-i01 の p040・p044 の後、コンテキストの都合で未着手のまま返却。次の generation のための手がかりを下に書いた）
Parent: [WS245](../ws.md)
Queue: q1236-i01（P6）
設計: [design.md](../design.md) §6.2 の p045 の行（改訂 3a の p014 の範囲）、§1.1 の表 P1 の ICL の行、§1.2 の表 P2 の display 11 の列、§2.6（MG PLL）、§2.8（流れ B の ICL、MG PHY）、§3.1（`device.c` に ICL の ID を足さない、§7 #30）

## 受け入れ（design §6.2）

ICL の記述で eDP（transcoder EDP）・HDMI（combo B）・DP-TC（MG）の列が Linux の手順と表の値に一致、DMC v1 の parse、ICL の TC の connect の列。host だけ（実機は無い:「未確認」）。

## 着手の手がかり（p040・p044 の形に合わせて）

- **platform の行**（`intel/display-tables-xd.h`、`check-xd-tables-linux.py` も ICL に）: PCI ID（Linux `i915_pciids.h` の ICL_11_IDS）、stepping、port（A・B combo、C〜F = TC1〜4 の MG PHY: `I915_XD_PHY_MG` を足す）、PCH ICP、well（Linux `icl_power_wells`）、PLL（DPLL0/1、TBT、MG PLL 1〜4: enable は `MG_PLL_ENABLE` の場所 0x46030+4n）、CDCLK（`icl_cdclk_table`、voltage の段 3 つ）、DBUF（2 slice、`icl_allowed_dbufs`）、WM（latency +2 µs）、swing（`icl_combo_phy_trans_*`、`icl_mg_phy_trans_*`）、DMC（v1 の path）、pipe DMC 無し。`tc_flow` は `I915_XD_TC_FLOW_FIA`、`tccold` は新しい `I915_XD_TCCOLD_AUX_WELL_EXIT`。
- **xd-tc の流れ B の ICL の差**（p044 の `drv_i915_xd_tc_fia`・`_cold_functions`・`_connect` に field で）: ICL の FIA は FIA1 だけで modular の bit を読まない（Linux `icl_tc_phy_init`）→ 記述に field（例「FIA を modular と読む」）を足す。TC cold は legacy だけ port の AUX の well（+ pcode の EXIT_TCCOLD: xd-power の AUX の well の enable で、Linux `icl_tc_cold_exit` 3 回・成功後 1 ms）、DP-alt と TBT は無し。`kind` で分岐しない（p044 の grep が落とす）。
- **MG PHY**（xd-tc に MG の直の register の版）: DP_MODE（`MG_DP_MODE`、Linux `icl_program_mg_dp_mode` の ICL の分岐）、swing（`MG_TX1/2_SWINGCTRL`・`DRVCTRL` などの Linux `icl_mg_phy_set_signal_levels`）、uc_health の相当。
- **MG PLL**（xd-clock の TC PLL に display 11 の版）: `drv_i915_xd_tc_pll_words` は今 Dekel だけ（`tlinedrv` 1）。ICL は `tlinedrv` 2、loop filter（`MG_PLL_LF`）・`FRAC_LOCK`・SSC・BIAS・TDC の語と mask（Linux `icl_calc_mg_pll_state` の `!is_dkl` の道）、write は `icl_mg_pll_write`（window 無し）。Linux の text との全数の比べは p040 と同じ scratch の方法（`plan/ws051/tests/host-dkl.sh` の抜き出しで `is_dkl` を偽に）。
- **transcoder EDP**（xd-pipe）: DDI A の eDP は transcoder EDP、TRANS_CLK_SEL を書かない、`TRANS_DDI_FUNC_CTL` の EDP の input select。DP_TP_CTL/STATUS は port ごと（xd-ddi の `link->trans` の代わりの place）。
- **DMC v1**（xd-dmc の package の parse）、**PORT_CL_DW12 と WA #1178**、ICL の init の WA（xd-core）、port F。
- 衝突に注意: xd-pipe・xd-ddi・xd-dmc・xd-core は P1 の p050b〜d が殻から呼ぶ所。足す形（新しい field と分岐）にして、既存の関数の引数は変えない。
- 試験: `host-xd-cover-test` の「Ice Lake の ID は行が無い」は ICL の行を足したら ICL の行の check に置き換える（今は `unknown` に 0x8a52）。
