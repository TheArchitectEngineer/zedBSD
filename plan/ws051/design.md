# WS051 設計: USB-C の DisplayPort Alternate Mode

Status: 案の第 1 版（2026-10-04、ws051-p001、P1 generation16）。design-reviewer の敵対的レビューは未実施（次の手順）。人間の判断が要る点は §10。

## 1. 目的と範囲

Dell Latitude 5330（Alder Lake-P、display version 13）の USB-C の port（TC1・TC2）につないだ DisplayPort の display（USB-C の monitor、
USB-C→DP・HDMI の変換）に、DP Alternate Mode（DP-alt）で画面を出す。抜いて差し直すと画面が戻る。

範囲外（§10）: Thunderbolt の alt mode（TBT-alt。TBT の dock の先の DP。TBT PLL と TC cold off の power well（PCODE の handshake）が要る）、
DP MST、DSC、複数の display を同時に出すこと（mirror・拡張）、HDMI の alt mode、USB4 の DP tunnel。

## 2. Alder Lake-P の DP-alt の分担

| 段 | 誰が | OS の display driver から見えるもの |
| --- | --- | --- |
| USB PD の VDM で DP mode に入る、pin の割り当て（C/D/E）を決める | PD controller と EC の firmware（PPM） | 何もしない |
| Type-C の mux と FIA の lane の割り当て、PHY の ready | IOM（TCSS の firmware、PMC の mux） | `TCSS_DDI_STATUS` の READY、FIA の `PORT_TX_DFLEXDPSP`（lane の割り当て）・`DFLEXPA1`（pin）、DE の HPD の ISR の TC の bit |
| PHY の ownership を display に取る、TC cold を防ぐ | i915 | `DDI_BUF_CTL` の `TC_PHY_OWNERSHIP`、AUX_USBCn の power well（ADL-P の DP-alt・legacy は AUX の domain で TC cold を防ぐ） |
| AUX、DPCD・EDID、link training、PLL、DDI・transcoder・pipe | i915 | 普通の DP と同じ（PHY は Dekel（DKL）、PLL は TC PLL n（DKL PLL）） |
| HPD（抜き差し・IRQ_HPD） | i915 | DE の HPD の割り込み（`GEN11_DE_HPD_ISR` の TC の bit）と live status |

これは Linux v6.8.12 の i915 の `display/intel_tc.c`（ADL-P は `adlp_tc_phy_ops`）の分担と同じ（MIT。移植は `plan/ws031/i915-rebuild-rules.md` の
規則で行う。参照の source は `plan/ws031/linux-parity/linux-reference/i915-src/display/`）。**UCSI（WS050）は DP-alt の画面の出力に必須でない**:
mode に入るのは firmware で、i915 は TCSS・FIA の register で状態を読む。UCSI は補助の情報（partner の Alternate Mode の一覧、cable、診断）に
留まる（WS050 design §8 と一致）。ただし「firmware が OS の指示（UCSI の SET_NEW_CAM）なしに DP mode に入る」ことは 5330 の実機で確かめる
（§9 の UAT の 1 項目。入らないなら WS050 の SET_NEW_CAM が要り、WS050 §10-2 の判断と結び付く）。

## 3. 5330 の事実（[WS031 の参照の dump](../ws031/display-ref/)、Linux 6.8 の i915、VFIO の passthrough）

- connector: eDP-1（DDI A、combo PHY A）、HDMI-A-1（DDI B、combo PHY B）、DP-1・DP-2（DDI TC1・TC2、`AUX USBC1/DDI TC1/PHY TC1`・`AUX USBC2/...`）。
- VBT の child: DVO port DP-F（0x0d、AUX-F）と DP-G（0x0f、AUX-G）が「Thunderbolt port: yes」「DP USB type C support: yes」、
  DP の最大の link rate 8.1 Gbps（HBR3）。
- PLL: DPLL0・DPLL1（combo）、TBT PLL、TC PLL 1〜4（Linux の `adlp_plls`、`dkl_pll_funcs`）。
- power well: xelpd の map に AUX_USBC1〜4・DDI_IO_TC1〜4。
- dump の時点では TC の port に何もつないでいない（register の正解値は無い。§7 で bare metal の Linux で採る）。

## 4. 今の zedBSD の i915 の display（2026-10-04 の調査）

- 出力は 1 つ（`output.c`）: eDP か DDI B の HDMI を起動時に 1 回選び（`display=` の値、HDMI の sink があれば HDMI、2026-09-29 のユーザーの判断）、
  hotplug を追わない。
- combo PHY（A・B）だけ（`phy.c`）。**Type-C は未移植**: `intel_tc.c` 全体（`hotplug.c` の「intel_tc_port_connected() is not ported and answers
  false」）、DKL PHY とその buffer translation、TC PLL（`takeover.c` は MG/TC PLL の読み出しの表だけ、「icl_ddi_tc_get_pll not ported」）、
  外部 DP の detect（`intel_dp_detect` は「not ported and answers unknown」）、外部 DP の HPD の pulse（`intel_dp_hpd_pulse`）。
- 既にあるもの: AUX の転送（`aux.c`、TGL+ の register の選び方。TC の AUX の channel で動くかは要確認）、eDP の link training（`dp.c`）、
  DE・SDE の HPD の割り込みの有効化（`interrupts.c` が TC と TBT の bit を有効にする）、xelpd の power well の表（AUX_USBC・DDI_IO_TC を含む。
  ICL の AUX の ops の TC の分岐が正しいかは要確認）。`power.c` の「TC cold-off well is not built」は ADL-P では TBT-alt にだけ要る（DP-alt は
  AUX_USBC の domain、Linux の `adlp_tc_phy_cold_off_domain`）。
- **既存の誤り（要修正）**: `takeover.c` の VBT の DVO port の code が Linux と違う（`I915_DVO_PORT_DPG` 16・`HDMIG` 15、Linux は DPG 15・HDMIG 16）。
  XXX の注は「ADL-P の TC1〜TC4 はそこに達しない」と言うが、**5330 の TC2 の child は DP-G（0x0f = 15）なので HDMI-G と読み違える**。p002 で直す
  （WS031・WS075 の担当の source なので、直す前に Q1 に知らせる）。

## 5. 移植の範囲（Linux の関数、ADL-P の分だけ）

| 段 | Linux v6.8.12 | zedBSD の行き先（案） |
| --- | --- | --- |
| TC の port の状態 | `intel_tc.c`: `intel_tc_port_init`、`adlp_tc_phy_ops`（`hpd_live_status`・`is_ready`・`take_ownership`・`is_owned`・`get_hw_state`・`connect`・`disconnect`・`init`）、`tc_phy_load_fia_params`、`intel_tc_port_max_lane_count`・`intel_tc_port_get_max_lane_count`、`tc_phy_get_current_mode`、`tc_phy_verify_legacy_or_dp_alt_mode`、`intel_tc_port_connected`、`intel_tc_port_lock`・`unlock`・`get_link`・`put_link`（参照の数を数える最小の形） | `display/tc.c`（新）、`intel/tc.h`（register の定義） |
| TC cold と power | `tc_cold_block`・`unblock`（ADL-P の DP-alt・legacy は AUX_USBCn の domain）、`intel_display_power_legacy_aux_domain`、ICL の AUX の power well の TC の分岐（`icl_aux_power_well_enable` の TC） | `display/power.c` の確認と補い |
| DKL PHY | `intel_dkl_phy.c`（index の register を lock の下で読み書き）、DDI の `tgl_dkl_phy_set_signal_levels`、`intel_ddi_buf_trans.c` の `tgl_dkl_phy_trans_dp_hbr`・`hbr2` | `display/dkl-phy.c`（新）、`phy.c` の buffer translation の選び方 |
| TC PLL | `intel_dpll_mgr.c` の `dkl_pll_funcs`（`dkl_pll_enable`・`disable`・`get_hw_state`）、`icl_calc_mg_pll_state`（DKL の値の計算）、`icl_compute_dplls`・`icl_get_dplls` の TC の port の分（TC PLL n を port に割り当てる）、`icl_ddi_tc_enable_clock`・`disable_clock`・`is_clock_enabled`・`icl_ddi_tc_get_pll` | `display/clock.c`（今の DPLL の管理）と `ddi.c` |
| 外部 DP の検出 | `intel_dp_detect`（DPCD・sink count・EDID）、`intel_dp_hpd_pulse`（長い pulse = 抜き差し、短い = IRQ_HPD、link の状態の確認）、AUX の I2C over AUX の EDID の読み（既存の `edid-read.c` の経路） | `dp-sink.c`・`hotplug.c` |
| link training・DDI の enable | 既存の eDP の link training を外部 DP（TC の port、lane 数は FIA の最大、rate は HBR3 まで）へ。`tgl_ddi_pre_enable_dp` の TC の段（`intel_tc_port_get_link`、`DDI_BUF_CTL` の lane の reversal は TC では無し） | `dp.c`・`ddi.c` |
| 出力の選択 | `output.c` に USB-C の DP を足す（§6） | `output.c` |

TBT-alt（TBT PLL、`TC_COLD_OFF` の well と PCODE）と legacy の mode（TC の port に直結の DP・HDMI。5330 には無い）は移さない（legacy は
`tc_phy_verify_legacy_or_dp_alt_mode` の分岐の名前だけ残す）。

## 6. 出力の model

- 今の「1 画面」の model を保つ（§10-2 の案）: `display=` に `usbc`（`usbc1`・`usbc2`）を足し、既定（`auto`・無指定）は外部の display を優先して
  HDMI → USB-C TC1 → TC2 の順に接続された最初の sink、無ければ eDP。HDMI と同じく、外部の display に出す間は panel を消す。
- hotplug（受け入れの「抜いて差し直すと戻る」）: 起動時に選んだ USB-C の port の抜き差しを追う。抜けたら pipe を止めて PHY を disconnect、
  同じ port に再び sink が来たら connect・link training・modeset をやり直す。起動の後に別の port や panel に出力を移すことはしない（§10-3）。
- mode: HDMI と同じ規則（`display.mode=`、EDID の preferred、CVT の計算）。DP の link の帯域（lane 数 × rate）に入る mode だけ。

## 7. 試験

- host: register の列を記録・比較する fixture（i915 の既存の host の試験の形）で、TC の状態の判定（live status・ready・ownership・FIA の lane）、
  DKL PLL の値の計算（Linux の値と同じ）、buffer translation の表の選び方を確かめる。
- 正解値: bare metal の 5330 で Linux（6.8.12）を起動し、USB-C の DP の monitor をつないで `intel_reg` と debugfs で TCSS・FIA・DDI_BUF_CTL・
  DKL PLL・DKL PHY・link training の register を採る（code は写さず値だけ）。VFIO の passthrough では IOM・PMC が guest に渡らない見込みなので
  bare metal で（要確認）。
- compile: `plan/ws031/tests/i915-cc.sh`、kernel の build（warning 0）。
- QEMU: Type-C は無い。T1 の boot test（i915 を含まない image の回帰）と、i915 を含む image で TC の port が「未接続」で何もしないことは実機で。
- 実機（UAT）: (1) firmware が UCSI なしに DP mode に入るか（live status の DP-alt の bit、`TCSS_DDI_STATUS`）、(2) TC1・TC2 で USB-C の DP の
  monitor（と USB-C→HDMI・DP の adapter）に画面が出る、(3) 抜いて差し直すと戻る、(4) 起動時に何もつないでいないときに panel が出る（回帰）、
  (5) HDMI と USB-C の両方をつないだときの選択。

## 8. 他の WS との境界

- WS050（UCSI）: 必須の依存でない（§2）。UCSI の状態（DP の mode に入った事実、partner の SVID）は診断で並べて出すだけ。§9 の UAT の (1) で
  firmware が自分で入らないと分かったら、WS050 に SET_NEW_CAM を足す判断（WS050 §10-2）に戻る。
- WS049（ACPI）: 不要（TCSS・FIA は MMIO）。
- WS031・WS075（i915）: 同じ display の source を触る。`takeover.c` の DVO の code の修正（§4）と、`power.c`・`clock.c`・`ddi.c`・`dp.c`・
  `hotplug.c`・`output.c` の変更は WS075 の作業と重なりうるので、Phase ごとに Q1 が衝突を調整する。
- WS052（電源）: TC cold と display の power well は display 内で閉じる。S0ix で TC の PHY を手放す手順は WS052 の suspend の hook へ（後で）。
- WS035（zdesktop）: 1 画面の model のままなので変更なし（mode が変わるだけ）。

## 9. Phase の案

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | — | この文書、レビュー、§10 の判断 |
| p002 | TC の port の核（`tc.c`: live status・FIA・ready・ownership・TC cold・connect/disconnect）、VBT の DVO の code の修正、TC の HPD の割り込みから live status へ、診断の log | p001 | host の fixture の試験、compile・build warning 0、UAT の (1) の手順（register を log に出す） |
| p003 | DKL PHY と TC PLL（`dkl-phy.c`、DKL PLL、DDI の TC の clock、buffer translation） | p002、bare metal の Linux の正解値 | PLL の値の計算が Linux と同じ（host）、build |
| p004 | 外部 DP の検出と出力（TC の AUX、DPCD・EDID、link training、modeset、`output.c` の選択） | p003 | 実機で TC1・TC2 に画面が出る（UAT の (2)・(4)・(5)） |
| p005 | 抜き差し（HPD の長い pulse・IRQ_HPD、disconnect と再 connect） | p004 | 実機で抜いて差し直すと戻る（UAT の (3)） |
| p006 | 規約の全文の確認と最終の確認 | p002〜p005 | 規約、build、boot test（T1）、実機の回帰 |

## 10. 人間の判断が要る点

1. **TBT-alt を範囲外にする**案（USB-C の DP の monitor・adapter は DP-alt。TBT の dock の先の display は対象外）。
2. **1 画面の model を保つ**案（HDMI と同じく外部の display を優先し panel を消す。mirror・拡張は別の WS）。ws.md の受け入れの「mirror か拡張かは
   p001 で決める」をこの案で確定するか。
3. **hotplug の範囲**: 起動時に選んだ port の抜き差しだけを追い、別の port・panel への切り替えはしない案。
4. **ws.md の依存と前提の訂正**: 「Alternate Mode に入る・出る、pin の割り当て、HPD は UCSI（WS050）を通して」を「mode に入るのは firmware、
   pin（lane）は FIA、HPD は i915 の割り込み。UCSI は補助」に、範囲の「UCSI で DP Alt Mode に入り…」を「TCSS・FIA で DP-alt の状態を読み…」に直す案
   （Resume point の「WS050 が前提」も）。
