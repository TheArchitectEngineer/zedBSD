<!-- awesome-plan project=zedbsd record=ws245-p028 -->

# ws245-p028: combo DPLL（xd-clock）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i03（P1、ws245-p028 から残りの段を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p028 の行、§2.6、表 R の DPCLKA_CFGCR0 の行

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-clock.c/.h`（Zlib） | combo の port PLL: `struct i915_xd_dpll_words`（CFGCR0・CFGCR1・DIV0 の AFC と書くか）。`drv_i915_xd_dpll_dp_words`（表 T3、38.4 MHz は 19.2 の行、`frac_wa_38p4` の platform は小数を半分に丸め）・`_hdmi_words`（AFE = 5 × TMDS、divider の表のうち DCO が 7998〜10000 MHz で中央に最も近い物、P・Q・K に分け 15 bit の小数）・`_tbt_words`・`_port_khz`（語から link clock）・`_desc`（select の値で platform の PLL）・`_enable`（power → 待ち → CFGCR0/1 → DIV0 の AFC（TGL・display 13 の DPLL/TBT だけ）→ posting read → enable → lock、待ちの遅れを bit で返す）・`_disable`（全種: enable → lock が落ちる → power → power が落ちる）・`_read`・`_map`（select の rmw の後に別の書き込みで clock-off を消す）・`_unmap`・`_mapped`。map の場所は platform の `pll_map`（TGL/display 13 の 1 語、RKL の PHY D の select と clock-off、ADL-S の 2 語）。手順は block ごとに `Procedure referenced:`（intel_dpll_mgr.c の icl_calc_dp_combo_pll・icl_calc_wrpll・icl_wrpll_get_multipliers・icl_wrpll_params_populate・icl_wrpll_ref_clock・icl_calc_dpll_state・ehl_combo_pll_div_frac_wa_needed・icl_calc_tbt_pll・icl_ddi_combo_pll_get_freq・__cnl_ddi_wrpll_get_freq・combo_pll_enable・tbt_pll_enable・icl_pll_power_enable・icl_dpll_write・icl_pll_enable・icl_pll_disable・icl_pll_get_hw_state、intel_ddi.c の _icl_ddi_enable_clock・_icl_ddi_disable_clock・_icl_ddi_get_pll・_icl_ddi_is_clock_enabled・rkl/adls_ddi_enable_clock）、divider の表・DCO の範囲・待ちの 1 ms・P/K の code は `Value(s) referenced:` |
| `display/clock.c`（殻） | combo と TBT の PLL の hook（get_hw_state・get_freq・enable・disable）と TC PLL の disable を xd に。`drv_i915_icl_dp_combo_pll`・`_icl_hdmi_wrpll` は xd の語、`drv_i915_icl_compute_tc_phy_dplls` の TBT の語も xd。殻に残る物: pool・参照の数（`pll->state`・`active_mask`・`on`）、DISPLAY_CORE の取得、待ちの遅れを旧の文の error（`PLL %d not locked` など）に写す `i915_dpll_report`、modeset の backend への window（`i915_dpll_window`、backlight と同じ形）、DDI の map の口 `drv_i915_dpll_map_ddi`・`_unmap_ddi`・`_ddi_mapped`。2943 行（段の前 3519） |
| `display/ddi.c`（殻） | combo の DDI clock の enable/disable/get_pll/is_clock_enabled を xd に（DPLL lock は殻が取る: 表 R）。`_reg` の helper 4 つを削除 |
| `display/takeover.c`・`takeover.h` | `drv_i915_shared_dpll_init(d, platform)`: 記録を xd の platform の PLL の表から作る（旧の adlp_plls・tgl_plls と `struct i915_adlp_pll_desc` を削除）。nogem の DPLL の readout（enable と DPCLKA の select）、sanitize の PLL の disable（全種 xd の disable）、DDI clock の is_enabled/disable の combo の道を xd に。nogem に `struct i915_xd *xd` |
| `display/modeset.c/.h`・`modeset-internal.h`・`display.c` | lcd world に `xd_platform`（`drv_i915_lcd_set_xd_platform` を display.c が display version と一緒に設定、`drv_i915_lcd_xd_platform` は無ければ display version の最初の行: host 試験の world） |
| `display/internal.h` | `struct i915_dpll` に `xd_pll`、nogem に `xd` |
| `intel/clock.h` | combo の DP の表と TBT の表（Linux の型）を削除 |
| `intel/display-regs-xd.h`・`tests/display/xd-regs-map.h` | ADL-S の PHY D・E の clock-off の bit（`Values referenced:` ICL_DPCLKA_CFGCR0_DDI_CLK_OFF）と 2 つの対 |
| `tests/display/host-xd-dpll-test.c`（新）・`plan/ws245/tests/run-xd-host-tests.sh` | 30 の check（下） |
| `tests/display/host-lcd-modeset-test.c` | 順の check の「PLL lock」を CFGCR1 の後の enable の書き込みに、DDI clock を DPCLKA の書き込みに（xd は rmw と wait を read と write にする）。E の time base の fault を PLL の待ちから DP_TP_STATUS の待ちに（PLL の待ちは xd の poll で backend の wait_reg を通らない） |

段の phase.md の必須の項目:
- 消す file: 無し（`clock.c` は TC PLL（Dekel）の部分と pool が p040・p050 まで残る）。
- 消した関数（本番の呼び手 0、`git grep`）: clock.c の static 22（i915_ehl_combo_pll_div_frac_wa_needed・i915_icl_calc_dp_combo_pll・i915_icl_calc_dpll_state・i915_combo_pll_enable_reg・i915_icl_dpll_write・i915_adlp_cmtg_clock_gating_wa・i915_icl_pll_disable・i915_icl_wrpll_ref_clock/_get_multipliers/_params_populate・i915_icl_calc_wrpll・i915_icl_ddi_combo_pll_get_freq・i915_icl_pll_get_hw_state/_read_config・i915_icl_calc_tbt_pll・i915_combo_pll_enable/_disable/_get_hw_state（同名の殻を作り直した）・i915_tbt_pll_get_hw_state/_enable/_disable・i915_mg_pll_disable）、ddi.c の static 4（i915_icl_ddi_enable_clock_reg・_disable_clock_reg・_get_pll_reg・_is_clock_enabled_reg）、takeover.c の static 3（i915_icl_dpclka_ddi_clk_off・i915_nogem_wait_clear・i915_nogem_icl_pll_disable）。残る殻の呼び手: `drv_i915_icl_dp_combo_pll`・`_icl_hdmi_wrpll`（state.c:845・:887）、`drv_i915_dpll_get_hw_state`・`_get_freq`（ddi.c の readout）、`drv_i915_enable/_disable_shared_dpll`（pipe.c）、`drv_i915_dpll_map_ddi` ほか（ddi.c）、TC の `i915_icl_pll_power_enable`・`i915_icl_pll_enable`（mg の enable だけ、p040 で消す）。
- 殻が更新する旧の写し: `pll->state.hw_state`（readout の cfgcr0/1/div0）、`pll->on`・`active_mask`（旧の pool のまま）、crtc の `shared_dpll`（DDI の select の読みから）、nogem の `dplls[].on`・`shared_dpll_id`。写しの同期は host-lcd-modeset-test（A の PLL と DPCLKA の値、D の順）と旧の host 試験の書き込みの列の比べで見た。
- 表 R: DPCLKA_CFGCR0 は TC の clock-off（`ddi.c:4660`・`:4688`、旧）と combo の map（xd）が rmw。xd の map も殻が `display.dpll.lock` を取って呼ぶ。nogem（起動の 1 回、modeset の前）も rmw。
- hook と lock: modeset の hook（enable/disable/get_hw_state）は旧の pool と DPLL lock の下、window は modeset の backend の read32/write32/usleep。nogem は `display->xd.io`（本物の MMIO）。
- R11: 触った file は全部 0（ddi.c 3 → 0、modeset.c 23 → 0、modeset.h 1 → 0。comment の中の WS・Phase・Bug の参照を消した）。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）は段の前（47d0e157a の `git archive`）と出力が同じ。`host-lcd-modeset-test`（123）は、旧の rmw を（前の値と clear・set から）書いた値に直して比べ、**書き込みの列（番地と値と順）が全部一致**（scratch の `wr.py`）。違いは rmw と wait が read と write と read（poll）になった事だけ。
- 違い（Linux に合わせた）: HDMI の divider の選び方。旧は「中央からの距離」を符号無しの引き算のまま比べ（comment の XXX）、中央より上の最小の DCO を選んでいた。Linux v6.8.12 の `abs()` は符号無しの値も符号付きに直して絶対値を取るので、本当の距離で選ぶ（決定 4: Linux を優先）。25.2・74.25・148.5・154・162・297・594 MHz は同じ、27・54・108・241.5 MHz などは divider が変わる（25〜600 MHz を 250 kHz 刻みで 2301 中 423）。
- 違い: ADL-P A0 の CMTG の WA（Wa_16011069516）は持ち込まない（§7 #14「支えない」）。5330 は revid 0x0c。
- 違い: nogem の sanitize が使われていない combo PLL を止める時、旧は enable の bit だけを消していた。xd は Linux の disable の全部（enable → lock → power → power）。
- 違い: 語に divider の無い PLL の link clock は 0（旧は不明な code をそのまま割り算に使った）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- survey の DPLL0 の ENABLE（0xcc000000）・CFGCR0（0x00e001a5）・CFGCR1（0x88）・DPCLKA_CFGCR0 が基準と同じ。`get_config: ICL_DPCLKA_CFGCR0=… -> shared_dpll id 0`（PHY A → DPLL 0）を引き継ぐ。`PLL … not locked` などが無い。
- 節目（ユーザーの一覧）: 5320 の起動（TGL の表の行）。外部出力（HDMI = DDI B の wrpll）: 未確認（host 試験だけ）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p028/xd-host` → regs 500・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 65・dmc 32・clock 18・dpll 30、失敗 0。`I915_XD_STRICT` の検査は通る。
- dpll の check: HBR@38.4 = 0x00e001a5/0x88（ADL-P・TGL）、RKL は小数をそのまま（0x01c001a5）、24 MHz の行、AFC 5 = 0x0a000000、表に無い rate は EINVAL、全部の DP の行（19.2・24・38.4）が link rate に戻る、TBT = 0x00600054（5330 の m3 と同じ）。HDMI 148.5 MHz = 0x001001d0/0xe84（divider 12）、27 MHz は 66（中央に近い方）、25.2〜594 MHz が 1 kHz 以内で戻る、合わない clock は EINVAL。enable の列（power → CFGCR0 → CFGCR1 → enable、SSC と DIV0 を書かない、0xcc000000）、disable の列、DPLL 1 の DIV0 の AFC、RKL の DPLL 4（0x164294、DIV0 無し）、lock しない・lock が落ちない時の遅れの bit、TC PLL は書かない、ADL-P の select 0〜6 と enable の番地（旧の `host-vbt-pll.sh` の PLL の check を移した）。read: 5330 の DPLL 0 が Linux の state（2.7 GHz）、AFC は override の時だけ、DPLL 1 は off。map: PHY A の 2 回の書き込み（0x00000c00、0x00000800）、PHY B の select と gate、RKL の PHY D（bit 27・13）、ADL-S の PHY E（2 語目）。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p028/linuxcheck` → 1021 対が一致、差 0。
- 旧の host 試験と書き込みの列の比べ: 上。master の Tools の `plan/ws051/tests/host-tc.sh` は 112・34 で失敗 0。**`plan/ws051/tests/host-vbt-pll.sh` は `FAIL pll-count: 0 rows`**（takeover.c の adlp_plls の行を sed で取り出す道具。表は xd の platform の表に移した）。DVO の 2 つの check は PASS。PLL の check は host-xd-dpll-test に移したので、Q1 に Tools の行の更新（道具から adlp_plls の部分を外すか、DVO だけの道具に直す）を頼む（design §6.1 R4）。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p028` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
