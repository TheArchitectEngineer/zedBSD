<!-- awesome-plan project=zedbsd record=ws260-p004 -->

# ws260-p004 数の計算を xd-clock・xd-dp・xd-ddi へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `intel/display-regs-xd9.h`（新規）: display 9 の CDCLK_CTL・LCPLL1_CTL・DPLL_CTRL1/2・link rate の符号・DPLL_CFGCR1/2 の field（PRM Vol 2c を出典に）。
- `xd-clock.c/h`: `drv_i915_xd9_cdclk_khz()`（VCO と frequency select の 2×4 の表）、`drv_i915_xd9_dpll_dp_words()`（port clock と link rate の符号の表）、`drv_i915_xd9_dpll_hdmi_words()`（HDMI の WRPLL: PRM の DPLL の周波数の計算。中心の周波数・除数の表（P・Q・K の符号に分けた data）と、偏差の判定の helper、偶数の除数を先に、同じ偏差は先に見つけた物。参照は display 9 の 24 MHz だけ）。`struct i915_xd9_dpll_words`。
- `xd-dp.c/h`: `drv_i915_xd9_dp_link()`（RBR・HBR・HBR2 を sink の上限まで、1・2・4 lane を既存の `drv_i915_xd_dp_choose()` で選び、M/N は既存の `drv_i915_xd_dp_m_n()`）、`drv_i915_xd9_dp_sink_rate()`、`drv_i915_xd9_dp_rate_code()`。
- `xd-ddi.c/h`: `drv_i915_xd9_ddi_buf_table()`（行の DDI buffer の組から DP・eDP・HDMI の表、DDI A 以外は DP・eDP を 9 行に）、`drv_i915_xd9_ddi_hdmi_row()`。signal level と最高の swing は既存の `drv_i915_xd_combo_level_row()`・`drv_i915_xd_dp_swing_max()` がそのまま同じ対応（DP の swing と pre-emphasis の組の順）。
- `xd-platform.h/.c`・`display-tables-xd9.h`: `struct i915_xd9_buf_trans`・`_buf_table`・`_ddi_buffers`、行に `power_class`（H・U・Y）と `ddi_buffers`（display 11〜13 の 11 行は H と NULL を足した）。DDI buffer の 11 の表（Linux の intel_ddi_buf_trans.c の値の引用、block に `Values referenced:`）と 6 つの組（SKL の H・U・Y、KBL の H・U・Y。CFL・CML は KBL の組）。`drv_i915_xd_platform_display9_class(kind, class)`。
- `gen9-mode.c`: cdclk・dp_link・dp_m_n・dpcd_rate_khz・dpcd_rate_code・dpll_dp・dpll_hdmi・sku・buf_trans・signal_level・voltage_max・hdmi_level を xd を呼ぶ殻にし、DDI buffer の表・signal level の表・ULT/ULX の id の list・M/N の helper・DCO の定数を消した（1402 → 885 行）。`struct i915_skl_buf_trans` は xd の `struct i915_xd9_buf_trans` に置き換えた（gen9 の file と試験の型の名前だけ）。
- 試験: host-xd-display9-test に行の `power_class` と mode set の power class の一致を足した。

## 確かめ（2026-10-18）

- golden（`run-golden.sh build/tmp/ws260-p004-g2`）: modeset・outputs・backlight は same。table は `dpll_hdmi 19200 …` の 2053 行だけが違う（下の意図した違い）。他の 93,566 行（24 MHz の WRPLL、DP の link と M/N、CDCLK、DPLL の DP の語、sku・buf trans・signal level・voltage max・HDMI の level を含む）は同じ。
- 意図した違い: HDMI の WRPLL は display 9 の参照の 24 MHz だけを受ける（今の呼び手は全部 24 MHz）。前の code は 19.2 MHz の時に整数部を 19.2 MHz、小数部を 19 MHz で割る食い違った語を出していた。xd 版は 19.2 MHz を EINVAL にする。golden の表をこの違いで記録し直した（Q1 に報告済み）。
- gen9 の host 試験: mode 71・modeset 80・outputs 29、0 failures。xd9 host 試験 24 checks PASS。
- xd host 試験・kbuild は下の追記。
- xd host 試験（`run-xd-host-tests.sh build/tmp/ws260-p004-xd`）: all passed。xd9 host 試験: all passed。`kbuild.sh p004`: 3 組とも rc 0、warning 0。
- golden を `--record` で記録し直し（table の 19.2 MHz の行だけが変わる）、`run-golden.sh build/tmp/ws260-p004-g3` で 4 つとも same。
