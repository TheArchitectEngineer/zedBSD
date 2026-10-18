<!-- awesome-plan project=zedbsd record=ws245-p040 -->

# ws245-p040: Dekel の窓と TC PHY、TC PLL（xd-tc、dkl-phy.c と clock.c の TC の部分）

Status: in-progress（2026-10-17 P6: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1236-i01（P6。P1 の p050b〜d と並行: 2026-10-16 ユーザー「P1,P3の処理で、並列にオフロードできる実装があれば、それぞれ1エージェントずつ追加してほしいです。…並列な実装は完璧でなくてもよく、あとでマージするときに整合が必要でもOKです。」）
Branch: agent/p6-ws245（P1 の agent/p1-ws245 の 5342e0844 から）
設計: [design.md](../design.md) §6.2 の p040 の行、§2.6（TC PLL）、§2.8（Dekel の swing・DP_MODE）、§3.4（Dekel の窓の lock は 1 つ）、§6.7（p006 の途中の差分の xd-tc.c と regs の field）

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-tc.c/.h`（新） | `struct i915_xd_dkl`（Dekel の窓: xd の io、port が共有する 1 つの spinlock、ready、別の道の hook）。`drv_i915_xd_dkl_init/_set_hooks/_window/_read/_write/_rmw`、`drv_i915_xd_tc_wait_health`（PHY の microcontroller の health、10 µs ごと）、`drv_i915_xd_tc_dp_mode`（pin assignment と lane 数 → 2 lane の DP_MODE）、`drv_i915_xd_tc_swing_table`（HDMI・DP ≤2.7 GHz・それより上）、`drv_i915_xd_tc_signal_levels`（PMD_LANE_SUS・DPCNTL0/1 の行・DP20BITMODE を消す・`I915_XD_WA_DKL_LOADGEN` の platform は LOADGEN）、`drv_i915_xd_tc_release_soft_reset`（PCS_DW5 の CORE_SOFTRESET）。§6.7 の p006 の `xd-tc.c` を元に、窓の lock は xd に 1 つ（旧 `dkl-phy.c` の lock は消えた） |
| `display/xd-clock.c/.h` | TC PLL: `struct i915_xd_tc_pll_words`、`drv_i915_xd_tc_pll_words`（DP 8.1 GHz・HDMI の範囲の分周の探索、clock top、feedback の整数と 22 bit の小数、loop filter、iref、TDC、feed-forward、VBT の AFC）、`_port_khz`、`_enable`（enable 語の power → PHY に 8 語（使う field だけ、DIV0 は 1 回の rmw）→ posting read → enable → lock）、`_read`。TC port の DDI clock: `drv_i915_xd_tc_clock_select/_map/_unmap/_mapped`（DDI_CLK_SEL と DPCLKA_CFGCR0 の TC の clock-off） |
| `intel/display-regs-xd.h`、`display/xd-platform.h`、`intel/display-tables-xd.h` | Dekel の TC PLL の field と HSDIV の code（Values referenced）、WA の bit `I915_XD_WA_DKL_LOADGEN`（display 13 の 4 行） |
| `display/xd.h`・`xd-display.c` | `struct i915_xd` に `dkl`。`drv_i915_xd_start` が io の後に窓を用意 |
| `display/display.c`・`internal.h`・`power.c`（殻） | `display->dkl`（`struct i915_dkl_phy`）を消し、power well の context の `dkl` は `&display->xd.dkl`。TC AUX well の uc_health の hook は `drv_i915_xd_tc_wait_health`（1 ms） |
| `display/tc-kern.c`（殻） | modeset の emit の `dkl_read/write/rmw` の橋は `drv_i915_xd_dkl_*`（device の窓と lock）。R11 の 1 行（ws177-p003）を消した |
| `display/clock.c`（殻） | TC PLL の計算・周波数・書き込み・enable の旧 code（`i915_icl_calc_mg_pll_state`・`find_divisors`・`mg_pll_get_freq`・`dkl_pll_write`・`mg_pll_enable`・`tc_pll_enable_reg`・`icl_pll_power_enable/_enable` と `drv_i915_dkl_pll_freq`）を消した。`drv_i915_dkl_pll_calc`・`drv_i915_icl_compute_tc_phy_dplls` と pool の TC PLL の hook（get_freq・enable）は xd-clock を呼ぶ。`drv_i915_lcd_xd_dkl`（modeset の Dekel の道: xd-tc の hook を modeset の `i915_lcd_dkl_phy_*` に向ける） |
| `display/ddi.c`（殻） | TC の 4 か所の中身だけ: DP_MODE（`drv_i915_xd_tc_dp_mode`）、CORE_SOFTRESET の WA（`drv_i915_xd_tc_release_soft_reset`）、swing（`drv_i915_xd_tc_signal_levels`）、TC の DDI clock（`drv_i915_xd_tc_clock_*`）。TC の get_buf_trans は xd の Dekel の表の view（長さと HDMI の既定。combo の view と同じ形）、Linux の Dekel の表を引く 5 関数を消した |
| `display/modeset-internal.h` | `drv_i915_dkl_phy_window` → `drv_i915_xd_dkl_window`（model の道の 4 か所）、`intel/dkl.h` の include を消し `enum tc_port` をここに移した、`drv_i915_lcd_xd_dkl` の宣言 |
| `display/dkl-phy.c/.h`、`intel/dkl.h` | 中身を消した（dkl-phy.c は vmunix.mk から外した）。file の削除は merge の時に Q1 |
| `tests/display/host-xd-tc-test.c`（新）、`plan/ws245/tests/run-xd-host-tests.sh` | 70 check（下）。runner に足した |
| `tests/display/xd-regs-map.h`、`host-xd-regs-test.c` | DKL の PLL の field の 24 対を足した。regs-test は `intel/dkl.h` を include しない（DKL の対は `check-xd-regs-linux.sh` が Linux の tree と比べる） |
| `platform/amd64/vmunix.mk` | dkl-phy.c を外し xd-tc.c を足した |

段の phase.md の必須の項目:
- 消す file: `display/dkl-phy.c`・`display/dkl-phy.h`・`intel/dkl.h`（中身は消した。削除は merge の時に Q1）。
- 消す関数とまだ残る呼び手: `drv_i915_dkl_phy_init/_window/_read/_write/_rmw/_wait_set`（呼び手 0）、`drv_i915_dkl_pll_freq`（呼び手 0）、clock.c の TC PLL の static 10 個、ddi.c の Linux の Dekel の表の 5 関数。残る殻: `drv_i915_dkl_pll_calc`（state.c:941 が呼ぶ）、`drv_i915_dkl_pll_describe`・`drv_i915_tbt_pll_describe`（pool）、`drv_i915_lcd_dkl_read/_write/_rmw`（tc-kern.c、modeset.c の emit の hook）、modeset-internal.h の `i915_lcd_dkl_phy_*`（emit の hook か model の窓）。これらは modeset・tc-kern を消す段（p050d・p052）で消える。`intel/phy.h` の Linux の Dekel の表（`tgl_dkl_phy_trans_*`・`adlp_dkl_phy_trans_*`）は呼び手 0 になった（phy.h ごと p052 で）。
- 殻が更新する旧の写し: TC PLL の `intel_dpll_hw_state` の `mg_*`（xd の語の写し、`i915_tc_pll_words_to_state`）。
- 表 R: HIP_INDEX（0x1010a0/0x1010a4）の書き手は xd-tc の窓だけ、lock は `display->xd.dkl.lock` の 1 つ（2 つの lock が同じ index を守る期間は無い）。modeset の道（`drv_i915_lcd_xd_dkl`）は emit の hook を通り、device では tc-kern.c の橋が同じ窓と lock に着く。host の model（emit の hook 無し）は窓の番地を write32/read32 で書く（旧と同じ）。
- 殻が xd に渡す hook と取る lock: `drv_i915_lcd_xd_dkl` の hook（modeset の `i915_lcd_dkl_phy_*` → emit → tc-kern.c → xd-tc の窓: 窓の spinlock だけ、modeset の lock の下）。xd-power の `tc_health` の hook（power.c、power domains の mutex の下 → 窓の spinlock）。窓の spinlock の下では xd の io の read/write（mmio と mmio-record の append）だけで、mutex を取らない。
- R11: 触った file の数は全部 0（tc-kern.c の 1 行を消した）。
- build の組: 既定と `I915_TESTS=y` の 5 組（下）。

## 旧と同じに保った物と違い

- TC PLL の語: Linux v6.8.12 の `icl_calc_mg_pll_state`・`icl_ddi_mg_pll_get_freq` の text（`plan/ws051/tests/host-dkl.sh` と同じ抜き出し、`plan/ws031/linux-parity`）と xd を、DP 1000 kHz・HDMI 250 kHz の刻みで 20〜700 MHz、参照 19.2/24/38.4 MHz で比べ、7172 件で語と周波数が全部一致、3034 件は両方が拒む（scratch の比べ、commit しない）。HDMI 148.5 MHz@38.4 の語を host-xd-tc-test に固定した。
- 書く register の列: TC PLL の enable は旧と同じ順（power → wait → REFCLKIN・CORECLK・HSCLK は read/write、DIV0 は rmw、DIV1・SSC・BIAS・TDC は read/write → TDC の posting read → enable → lock）。DP_MODE は 2 語の read の後 2 語の write。swing・LOADGEN・soft reset も旧と同じ rmw の列。
- 違い: (1) device の Dekel の窓の access が `drv_i915_raw_read32/write32`（記録しない）から xd の io（`drv_i915_read32/write32`、start の書き込みの記録 mmio-record と xd の io の ring に入る）に。start の記録の間に TC の AUX well が上がると digest に HIP_INDEX の書き込みが増える（5330 の素の起動で USB-C が無ければ増えない見込み）。(2) TC の DDI clock の DDI_CLK_SEL の書き込みが DPLL の lock の中に入った（書く順は同じ）。(3) TC の buf trans の view は `entries` を持たない（level の選びは長さと HDMI の既定だけを読む: combo と同じ）。(4) display 11 の「UNPORTED」の log は無くなった（display 11 は p045）。(5) uc_health の poll は 10 µs ごと（旧と同じ）、timeout は xd の 1 ms（旧の power.c の 1 ms と同じ値）。
- 旧の host 試験（host-dp-test・host-lcd-modeset-test）の前後の比べ: 下の「確かめた事」。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- 起動: `i915: TC:` の log が基準と同じ。digest の差が無い（USB-C を挿さない素の起動）。
- 節目（p052 の後、ユーザーの一覧）: USB-C DP（TC2）を挿して 2 画面。TC PLL 2 の語が `0x100/0xa00/0x6200/0x84269/0x1c0027/0x40002000/0x5e000000/0x52`（RBR）、PORTTC2_PLL_ENABLE の lock、DDI_CLK_SEL(TC2)=0x80000000、DPCLKA_CFGCR0 の TC2 の clock-off が消える、DP_MODE と DPCNTL の語が基準と同じ。TC の AUX well の uc_health の timeout の log が無い。
- 5320（TGL、ユーザーの一覧）: 起動に変化が無い（TGL の TC は p044 の流れ B まで未対応のまま）。

## 確かめた事

- host（2026-10-17）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p040/xd-host` → 全部 pass（tc 70、regs 623 ほか前の段と同じ）、`I915_XD_STRICT` の検査も通る。
- Linux の register の比べ: `sh plan/ws245/tests/check-xd-regs-linux.sh <linux-6.8.12> build/p040/check-regs` → 1157 対が一致、0 差（Linux の tree は P1 の worktree の `build/ws229-p001/linux/linux-6.8.12` を読むだけで使った）。
- build（2026-10-17）: `sh build/tmp/kbuild.sh p040`（P1 の kbuild.sh を p6 の worktree に写した物）で既定と `I915_TESTS=y` の 5 組（vke2・display・display_ktest・display_ktest2・execution）が rc 0・warning 0（既定の vmunix の sha256 の頭 7982f1d0d094d817）。`git diff --check` は空。
- 旧の host 試験（host-dp-test 72・host-lcd-modeset-test 120）を前の木（5342e0844）と比べ、dp・lcd とも出力が同じ（`build/tmp/oldtests.sh`、P1 の harness の写し）。どちらも TC の道を通らない（出力に TC PLL・HIP_INDEX が無い）ので、TC は host-xd-tc-test と下の WS051 の試験で見た。
- WS051 の試験: `sh plan/ws051/tests/host-dpext-link.sh build/p040/dpext-link` → 16 check pass（5330 の DP-2 の TC PLL の語 div0 0x84269 などが state.c → `drv_i915_dkl_pll_calc` → xd-clock で同じ）。`sh plan/ws051/tests/host-tc.sh` は **壊れた**（前の木では host-tc 112・host-tc-tables 34 が pass。後の半分 host-tc-tables が `dkl-phy.c` から窓の define と `drv_i915_dkl_phy_window` を sed で抜くため）。窓の 3 check は host-xd-tc-test の「windows」に移した。master の Tools の行の直し（host-tc.sh の DKL の 2 行と host-tc-tables.c の DKL の 3 check を消す。check は 34 → 31）は Q1 に依頼。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- TC1・TC2 の declared・PHY mode・readout の行が基準と同じ、digest の差は I・H の行だけ（USB-C なし）

満たしていない・未確認の基準と残る物:

- USB-C DP（TC2）の 2 画面と TC PLL の語、5320: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
