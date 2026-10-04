# WS051 設計: USB-C の DisplayPort Alternate Mode

Status: 案の第 3 版（2026-10-04、ws051-p001、P1 generation16）。WS050 §10 のユーザーの決定（[WS050 design](../ws050/design.md) の末尾）と、
WS051 §10 のユーザーの決定（この文書の末尾）、Guardrail の「GPU の driver の scanout の規則」（2026-10-04）を反映し（第 2 版）、第 1 版への
[敵対的レビュー](design-review-2026-10-04.md)（H1〜H8・M1〜M10・L1〜L4）を反映した（第 3 版、§11 に各項目の扱い、§12 に追加の設計）。

## 1. 目的と範囲

Dell Latitude 5330（Alder Lake-P、display version 13）の USB-C の port（TC1・TC2）につないだ DisplayPort の display（USB-C の monitor、
USB-C→DP・HDMI の変換）を i915 が DP Alternate Mode（DP-alt）で駆動できるようにし、抜き差しを Vulkan の Display の拡張の経路で Keiland に知らせ、
Keiland の指示でその display に画面を出す（mirror・拡張・出力 off は Keiland が決める）。

**規則**（Guardrail「GPU の driver の scanout の規則」、2026-10-04 ユーザー「GPUドライバはGOPの出力先以外に、scanoutを開始しない」）: i915 は
boot の時に GOP が出していた出力先（port・pipe）を引き継いでそこにだけ出し、自分の判断で他の出力先（USB-C を含む）に scanout を始めない。
他の出力先に出すのは graphical session の Keiland の明示の指示（libvulkan の Display の拡張 → 既存の display の UAPI の `GPU_DISPLAY_CLAIM`・
`PRESENT`）の時だけ。逆に、GOP の出力先であれば、それが eDP・HDMI・DP（combo PHY）でも USB-C の DP-alt でも、i915 はその出力先で scanout
できるよう初期化を試みる（Guardrail の同じ節の補い、2026-10-04 ユーザー「GOPの出力先であれば、scanoutできるようにどのインタフェースでも初期化を
試みる」）。i915 がその種類の出力にまだ対応していない時は log に出して firmware の画面を保つ（GOP の framebuffer の出力を止めない）。

範囲外（§10・末尾の決定 1）: Thunderbolt の alt mode（TBT-alt。TBT の dock の先の DP。TBT PLL・AUX_TBT・`DP_AUX_CH_CTL_TBT_IO` が要る。
ADL-P には TC cold off の well は無い（TGL だけ、レビュー M1）。状態機械の TBT_ALT は「PHY を所有していない」状態として残す、§12）、
DP MST、DSC、HDMI の alt mode、USB4 の DP tunnel。mirror・拡張の構成と画面の配置は Keiland と WS113 の担当（この WS は i915 の側の出力と通知）。

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
mode に入るのは firmware で、i915 は TCSS・FIA の register で状態を読む（ユーザーの決定 5: WS051 は UCSI を待たずに進める）。i915 が読んだ
HPD・pin・lane 数・向き（取れれば）は WS050 の Type-C の層に報告し（`typec_display_report`）、UCSI 2.0 以上の値と統合する（決定 4・5 の補足、
WS050 design §13。画面を出す判断は常に i915 の値で行う）。ただし「firmware が OS の指示（UCSI の SET_NEW_CAM）なしに DP mode に入る」ことは 5330 の実機で確かめる
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
- 既にあるもの: AUX の転送（`aux.c`、USBC の register の選び方は正しい、レビューで確認）、eDP の link training（`dp.c`）、DE の HPD の割り込みの
  **有効化だけ**（`interrupts.c` は `GEN11_DE_HPD_IIR` を ack して数えるだけで hotplug へ渡さない、レビュー H5）、xelpd の power well の表
  （AUX_USBC1〜4・DDI_IO_TC1〜4、ただし AUX_USBC の well の enable に TC の分岐（`TBT_IO` の消去、DKL の `UC_HEALTH` の待ち）が無い、H2）、
  ddi.c の TC の分岐（Linux の順で既にあるが、呼ぶ先の多くが stub、H4）。
- **既存の誤り（要修正、レビューで判明）**:
  - **TC の AUX の power domain の計算**（H1）: `dp-internal.h` の `i915_aux_power_domain`、`modeset-internal.h` の
    `intel_display_power_legacy_aux_domain`、`pipe.c` の `drv_i915_aux_power_domain` が `AUX_A + (aux_ch - AUX_CH_A)` で、`AUX_CH_USBC1 = AUX_CH_D`
    なので TC1 で AUX_D を点け、AUX_USBC1 を点けない（TC cold を防げない）。Linux の d13 の port の domain の表に置き換える（p002b）。
  - **TC PLL の enable の register**（H8）: `takeover.c` が TC PLL n を `0x46030 + 4n`（MG_PLL_ENABLE）としているが、ADL-P は
    `ADLP_PORTTC_PLL_ENABLE`（TC1 0x46038、間隔 8）。readout が別の register を読み、sanitize が TC1 の PLL の bit を消しうる（p002、takeover の修正と一緒に）。
  - VBT の DVO port の code（`takeover.c` の DPG 16・HDMIG 15、Linux は DPG 15・HDMIG 16）: **第 1 版の「5330 の TC2 を読み違える」は誤り**
    （レビュー L1）。code 15 は zedBSD でも TC2 に写り、HDMI か DP かは device_type（0x68c6 = DP）で決まるので 5330 は読み違えない。誤るのは code
    17〜21（TC3・TC4）だけで 5330 には無い。`vbt.c` は既に正しい定義（`intel/vbt-defs.h`）を使っているので、takeover の写像を vbt.c のものに寄せる
    整理（急ぎでない）として p002 に残す。

## 5. 移植の範囲（Linux の関数、ADL-P の分だけ）

| 段 | Linux v6.8.12 | zedBSD の行き先（案） |
| --- | --- | --- |
| TC の port の状態 | `intel_tc.c`: `intel_tc_port_init`、`adlp_tc_phy_ops`（`hpd_live_status`・`is_ready`・`take_ownership`・`is_owned`・`get_hw_state`・`connect`・`disconnect`・`init`）、`tc_phy_load_fia_params`、`intel_tc_port_max_lane_count`・`intel_tc_port_get_max_lane_count`、`tc_phy_get_current_mode`、`tc_phy_verify_legacy_or_dp_alt_mode`、`intel_tc_port_connected`、`intel_tc_port_lock`・`unlock`・`get_link`・`put_link`（参照の数を数える最小の形） | `display/tc.c`（新）、`intel/tc.h`（register の定義） |
| TC cold と power | `tc_cold_block`・`unblock`（ADL-P の DP-alt・legacy は AUX_USBCn の domain）、`intel_display_power_legacy_aux_domain`、ICL の AUX の power well の TC の分岐（`icl_aux_power_well_enable` の TC） | `display/power.c` の確認と補い |
| DKL PHY | `intel_dkl_phy.c`（index の register を lock の下で読み書き、p002b へ前倒し: AUX_USBC の well の `UC_HEALTH` の待ちが要る、H2）、DDI の `tgl_dkl_phy_set_signal_levels`、ADL-P の表 `adlp_get_dkl_buf_trans`・`adlp_dkl_phy_trans_dp_hbr`・`adlp_dkl_phy_trans_dp_hbr2_hbr3`（第 1 版の tgl の表は誤り、H3）、`icl_program_mg_dp_mode`（pin の割り当てから DKL_DP_MODE の x1・x2）、`adlp_tbt_to_dp_alt_switch_wa` | `display/dkl-phy.c`（新）、`phy.c` の `get_buf_trans` の TC の分岐 |
| TC PLL | `intel_dpll_mgr.c` の `dkl_pll_funcs`（`dkl_pll_enable`・`disable`・`get_hw_state`）、`icl_calc_mg_pll_state`（DKL の値の計算）、`icl_compute_dplls`・`icl_get_dplls` の TC の port の分（TC PLL n を port に割り当てる）、`icl_ddi_tc_enable_clock`・`disable_clock`・`is_clock_enabled`・`icl_ddi_tc_get_pll` | `display/clock.c`（今の DPLL の管理）と `ddi.c` |
| 外部 DP の検出 | `intel_dp_detect`（DPCD・sink count・EDID）、`intel_dp_hpd_pulse`（長い pulse = 抜き差し、短い = IRQ_HPD、link の状態の確認）、AUX の I2C over AUX の EDID の読み（既存の `edid-read.c` の経路） | `dp-sink.c`・`hotplug.c` |
| link training・DDI の enable | 既存の eDP の link training を外部 DP（TC の port、lane 数は FIA の最大、rate は HBR3 まで）へ。`tgl_ddi_pre_enable_dp` の TC の段（`intel_tc_port_get_link`、`DDI_BUF_CTL` の lane の reversal は TC では無し） | `dp.c`・`ddi.c` |
| 出力の選択 | `output.c` に USB-C の DP を足す（§6） | `output.c` |

TBT-alt（TBT PLL、`TC_COLD_OFF` の well と PCODE）と legacy の mode（TC の port に直結の DP・HDMI。5330 には無い）は移さない（legacy は
`tc_phy_verify_legacy_or_dp_alt_mode` の分岐の名前だけ残す）。

## 6. 出力の model（末尾の決定 2〜4 と Guardrail の規則）

- **boot**: i915 は GOP から引き継ぐ時、GOP が出していた出力先（pipe・transcoder・DDI の port）を読み取り（今の `takeover.c` の GOP の状態の読み出し
  を広げる）、それをそのまま driver の出力先にする。USB-C の display が繋がっていても GOP の出力先を使う。今の `output.c` の「外部の display を
  優先し panel を消す」選択（`display=` の既定の auto・hdmi、2026-09-29 の判断）は規則に反するので廃止する（`display=` の値は GOP の出力先が
  無い（GOP が何も出していない・legacy の boot）時の fallback の選択にだけ残すか、削るかを p002 で決めて Q1 に報告）。GOP が USB-C（TC1・TC2）に
  出していた時だけ、boot で USB-C を引き継ぐ（その時は TC の PHY の ownership・TC cold の block を引き継ぎの中で取る）。GOP の出力先が i915 の
  まだ対応しない種類（p003〜p004 の前の USB-C、外部の DP の combo PHY など）なら、引き継ぎの初期化を試み、できない段で log に出して GOP の画面を
  そのまま保つ（pipe・plane・PLL を止めない、Guardrail の補い）。
- **graphical session**: Keiland（compositor）が libvulkan の Vulkan の Display の拡張で display を列挙し、hotplug の通知を受け、設定の記録などから
  mirror・拡張・出力 off を決める（WS113）。i915 の側は既存の display の UAPI（`include/uapi/gpu-display.h`）で:
  - `GPU_DISPLAY_QUERY`: TC1・TC2 の DP の output を列挙に加える（`GPU_DISPLAY_CONNECTED` は TC の live status と DPCD の sink count、
    mode は EDID から、`name` は "DP-1"・"DP-2" など Linux と同じ名前）。
  - `GPU_DISPLAY_EVENTS`: TC の HPD の長い pulse（抜き差し）で `GPU_DISPLAY_EVENT_CHANGE` を出す（generation を進める）。これが libvulkan の
    Display の hotplug の通知になる（WS113 の D1 と同じ経路）。
  - `GPU_DISPLAY_CLAIM`・`MODE`・`PRESENT`: Keiland がその display を claim して mode を選び present した時に初めて、TC の PHY を connect し、
    PLL・link training・DDI・transcoder・pipe を立てて scanout を始める。release と出力 off で pipe を止め PHY を disconnect する。
- **hotplug**: driver は出力先を自分で切り替えない。抜けたら、その display に scanout していれば pipe を止めて PHY を disconnect し、change の
  事象を出す。差されたら change の事象を出すだけ（scanout は Keiland の指示を待つ）。IRQ_HPD（短い pulse、link の状態の変化）は scanout 中の
  link だけを確かめ、要れば link training をやり直す。
- mode: EDID の preferred か Keiland が選ぶ mode。DP の link の帯域（lane 数 × rate）に入る mode だけを列挙する。

## 7. 試験

- host: register の列を記録・比較する fixture（i915 の既存の host の試験の形）で、TC の状態の判定（live status・ready・ownership・FIA の lane）、
  DKL PLL の値の計算（Linux の値と同じ）、buffer translation の表の選び方を確かめる。
- 正解値: bare metal の 5330 で Linux（6.8.12）を起動し、USB-C の DP の monitor をつないで `intel_reg` と debugfs で TCSS・FIA・DDI_BUF_CTL・
  DKL PLL・DKL PHY・link training の register を採る（code は写さず値だけ）。VFIO の passthrough では IOM・PMC が guest に渡らない見込みなので
  bare metal で（要確認）。
- compile: `plan/ws031/tests/i915-cc.sh`、kernel の build（warning 0）。
- QEMU: Type-C は無い。T1 の boot test（i915 を含まない image の回帰）と、i915 を含む image で TC の port が「未接続」で何もしないことは実機で。
- 実機（UAT）: (1) firmware が UCSI なしに DP mode に入るか（live status の DP-alt の bit、`TCSS_DDI_STATUS`）、(2) TC1・TC2 で USB-C の DP の
  monitor（と USB-C→HDMI・DP の adapter）に画面が出る、(3) 抜いて差し直すと戻る、(4) 起動時に USB-C の display を繋いでいても GOP の出力先（panel）がそのまま出る（規則）、
  (5) Keiland の指示で mirror・拡張・出力 off が USB-C に効く（WS113 と共同）。

## 8. 他の WS との境界

- WS050（UCSI）: 画面の出力の必須の依存でない（§2、決定 5）。i915 は TC の port ごとの HPD・pin・lane 数・向き（取れれば）を
  `typec_display_report` で WS050 の Type-C の層に渡す（WS050 p005）。WS050 の層が無い build では報告しない（weak の口か config で）。
  §7 の UAT の (1) で firmware が自分で DP mode に入らないと分かったら、WS050 の SET_NEW_CAM（決定 2 で範囲に入った、WS050 p004）を使う。
- **向き**（決定 4）: UCSI 1.x の機種では向きを i915 から取る。Linux の register の定義に plug の向きの bit は無いので、p002 の診断で FIA の
  `DP_LANE_ASSIGNMENT`（pin D の 2 lane の 0x3/0xC など）と TCSS の register を両向きの差し込みで記録し、向きと対応するかを確かめる
  （WS050 design §13）。対応しなければ取れないと Q1 に報告する。
- WS049（ACPI）: 不要（TCSS・FIA は MMIO）。
- WS031・WS075（i915）: 同じ display の source を触る。`takeover.c` の DVO の code の修正（§4）と、`power.c`・`clock.c`・`ddi.c`・`dp.c`・
  `hotplug.c`・`output.c` の変更は WS075 の作業と重なりうるので、Phase ごとに Q1 が衝突を調整する。
- WS052（電源）: TC cold と display の power well は display 内で閉じる。S0ix で TC の PHY を手放す手順は WS052 の suspend の hook へ（後で）。
- WS113（Keiland の外部 display・複数画面）: libvulkan の Display の拡張と compositor の側（hotplug の通知の受け取り、mirror・拡張・出力 off の
  判断、Settings）は WS113。WS051 は i915 の側で TC の output を display の UAPI に出し、事象を出し、claim・present で出力する。WS113 の D-ATOMIC
  などの契約の決定に合わせる（WS113 の p001 の契約を読み、食い違えば Q1 に）。
- WS035（zdesktop）: WS113 の経路で出力が増えるだけ。

## 9. Phase の案

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p001 | 調査と設計（この文書） | — | この文書、レビュー、§10 の判断 |
| p002 | **GOP の出力先の引き継ぎと外部優先の廃止**（Guardrail の scanout の規則、決定 2）: `takeover.c` が GOP の pipe・transcoder・port を読み取り driver の出力先にする、`output.c` の外部 display の優先をやめる、GOP の出力先の種類が未対応なら初期化を試みて GOP の画面を保つ、TC PLL の enable の register の修正（H8）、VBT の DVO の写像を vbt.c に寄せる整理（L1） | p001 | 実機（UAT の環境は §12 の H6）で GOP の出力先（panel・HDMI）がそのまま引き継がれ、外部の display があっても driver が切り替えない。host 試験で TC PLL の番地、QEMU の boot test（T1）、build warning 0 |
| p002b | TC の port の核（`tc.c`: live status・FIA・ready・ownership・TC cold・connect/disconnect、状態機械（TBT_ALT は未所有の placeholder）、init_mode・sanitize）、TC の AUX の power domain の修正（H1）、AUX_USBC の well の TC の分岐と DKL の index の読み（H2）、DE の HPD の割り込みの配送（`gen11_hpd_irq_handler`、long・short の判定、H5）、診断の log（向きの確かめの register の記録を含む） | p002 | host の fixture の試験（aux_ch と domain の表、connect の順と巻き戻し）、compile・build warning 0、UAT の (1) と向きの記録の手順 |
| p003 | DKL PHY と TC PLL（`dkl-phy.c` の残り、DKL PLL、`icl_ddi_tc_*`、`icl_update_active_dpll`、ADL-P の DKL の buffer translation、DP_MODE、FIA の lane 数） | p002b、正解値（§12 の H6） | PLL の値の計算が Linux と同じ（host）、build、PLL の lock の bit の読み戻しの診断（M7） |
| p004a | TC の AUX・DPCD・EDID の診断（modeset なし）、外部 DP の object の model（M6）、調べた後の同期の disconnect（M2）、branch device（protocol converter）の最小の扱いと sink count（H7） | p003 | 実機で TC1・TC2 の DPCD・EDID が読め、調べた後に PHY を手放す |
| p004b | display の UAPI での出力（`GPU_DISPLAY_QUERY` への TC の output、claim・mode・present での link training（fallback を含む、M3）・modeset・scanout） | p004a、WS113 の契約 | 実機で Keiland（か display の UAPI の試験の program）の claim・present で TC1・TC2 に画面が出る（UAT の (2)） |
| p005 | 抜き差しの事象（HPD の長い pulse で `GPU_DISPLAY_EVENT_CHANGE`、2 秒の猶予と 5 回の retry、scanout 中の抜けで pipe を止め PHY を disconnect、IRQ_HPD で retrain、M4）、S0ix の口（M10） | p004b | 実機で抜き差しが libvulkan の Display の通知に届き、差し直して Keiland の指示で画面が戻る（UAT の (3)） |
| p006 | 規約の全文の確認と最終の確認 | p002〜p005 | 規約、build、boot test（T1）、実機の回帰 |

外部の前提（Phase でない）: 正解値の採取（§12 の H6、人の作業と承認）。WS050 p004（SET_NEW_CAM）への条件付きの依存（UAT の (1) で firmware が自分で DP mode に入らない時）。

## 10. 人間の判断の点（2026-10-04 に決定済み。決定は末尾、この節は第 1 版の案の記録）

1. TBT-alt を範囲外にする案 → 決定 1 で採用。
2. 1 画面の model（外部優先で panel を消す）の案 → **不採用**。決定 2: driver は GOP の出力先を引き継ぎ自分で変えない、graphical session では
   Keiland が決める（§6、Guardrail の規則）。
3. hotplug の範囲の案 → 決定 3・4: driver は切り替えず、Vulkan の Display の拡張の通知で Keiland に知らせる（§6）。
4. （決定済み）ws.md の依存と前提: 2026-10-04 のユーザーの決定 5 とその補足で「HPD・pin は i915 の TCSS・FIA から、UCSI 2.0 以上で取れる時は
   UCSI からも。WS051 は UCSI を待たずに進める」と決まり、ws.md を直した。

## 11. レビューの項目の扱い

| 項目 | 扱い |
| --- | --- |
| H1 TC の AUX の domain | §4 の既存の誤り、p002b |
| H2 AUX_USBC の well の TC の分岐、DKL の前倒し | §5、p002b |
| H3 ADL-P の DKL の表 | §5（adlp の表に訂正）、p003 |
| H4 移植の範囲の不足と stub | §12 の表、各 Phase |
| H5 DE の HPD の配送 | §4、p002b |
| H6 試験の環境（VFIO と native） | §12、§13 の人間の判断 |
| H7 branch device・sink count | §12、p004a |
| H8 TC PLL の enable の番地 | §4、p002 |
| M1 TBT_ALT の状態と PCODE の誤り | §1、§12、p002b |
| M2 調べた後の disconnect | §12、p004a |
| M3 training の fallback | §12、p004b |
| M4 debounce・retry・retrain | §12、p005 |
| M5 起動時の TC と待ち | §12（GOP が USB-C の時、init_mode・sanitize・`icl_ddi_tc_get_pll`） |
| M6 外部 DP の model | §12、p004a |
| M7 Phase の分割 | §9（p004 を a・b に、診断を p003 に、外部の前提を明記） |
| M8 lock の順 | §12 |
| M9 向きを i915 から取る見込み | §12、Q1 経由でユーザーへ（決定 4 の見直し） |
| M10 S0ix | §12、p005、WS052 design の device の表 |
| L1 VBT の主張の誤り | §4 を訂正 |
| L2 TCSS_DDI_STATUS の pin・HPD_LIVE | §12（ADL-P では診断の値）、WS050 §13 の表に注記を依頼 |
| L3 採取の正 | §12（debugfs と drm.debug を正、intel_reg の DKL は参考） |
| L4 weak の口 | §8 の `typec_display_report` に理由の comment を付ける |

## 12. 追加の設計（第 3 版、レビューの反映）

- **移植と置き換える stub**（H4、Linux v6.8.12 の関数 → zedBSD の stub、file はレビューの行番号の時点）:

| Linux | zedBSD の stub | Phase |
| --- | --- | --- |
| `intel_tc_port_in_dp_alt_mode`・`intel_tc_port_in_tbt_alt_mode`（`intel_tc.c`） | `modeset-internal.h` の (0)・常に false | p002b |
| `intel_tc_port_lock`・`connected_locked`（`intel_tc.c:1813-1822` ほか） | `dp-internal.h` の無処理・常に true | p002b |
| `tc_port_power_domain`・ownership の DDI_LANES_TCn の domain（`intel_tc.c:245-252, 822-846`） | 無し | p002b |
| `intel_tc_port_init_mode`・`sanitize_mode`（`intel_tc.c:1465-1591`） | `takeover-internal.h` の STEP | p002b |
| link reset の一式（`intel_tc.c:1621-1745`） | `hotplug-internal.h`・`hotplug.c` の link_reset | p005 |
| `gen11_hpd_irq_handler`（`intel_hotplug_irq.c:655-688`）、`GEN11_TC_HOTPLUG_CTL` の rmw と long の判定 | `interrupts.c` の ack だけ | p002b |
| `intel_tc_port_get_link`・`put_link`・`set_fia_lane_count`・`get_pin_assignment_mask` | `modeset-internal.h` の STEP | p003 |
| `icl_program_mg_dp_mode`（`intel_ddi.c:2082-2164`） | `modeset-internal.h` の GUARD（TC で error） | p003 |
| `icl_update_active_dpll`・`intel_ddi_update_active_dpll`・`icl_ddi_tc_port_pll_type` | STEP | p003 |
| `adlp_tbt_to_dp_alt_switch_wa`（`intel_ddi.c:3495-3504`） | ADLP_WA の STEP | p003 |
| `intel_ddi_hotplug`（TC の 5 回の retry、`intel_dp_retrain_link`、`intel_ddi.c:4515-4575`） | 無し | p005 |
| `intel_dp_configure_protocol_converter`・downstream の上限 | `modeset-internal.h` の GUARD（branch で error） | p004a |

- **状態機械**（M1）: TBT_ALT を「PHY を所有していない」状態として Linux と同じに残す（既定の mode、connect の失敗の後の状態）。TBT_ALT では出力
  せず（`connected_locked` が BIT(mode) で絞るので detect は disconnected）、TBT の AUX（`intel_display_power_tbt_aux_domain` は INVALID）を呼ばない。
- **調べた後の disconnect**（M2）: AUX で port を調べたら、出力しない port の PHY の ownership を同期で手放す（Linux の put_link の flush と同じ。
  firmware は他の TC の port の HPD を、この port の PHY が disconnect されるまで更新しない）。遅延の work は移さず同期の disconnect に置き換える。
- **training の失敗**（M3）: fallback（rate を下げ、次に lane を減らす）を移す。全て失敗したら claim・present に error を返し、Keiland が別の出力を
  選ぶ（driver は自分で panel に戻さない、Guardrail の規則）。
- **debounce・retry**（M4）: 抜けても 2 秒待ち、まだ link の reset が要る時だけ止める。差した直後の detect は 5 回 retry。IRQ_HPD は retrain。
- **起動時**（M5）: GOP が USB-C に出していた時は init_mode・sanitize・`icl_ddi_tc_get_pll`（TC PLL の readout）で状態を引き継ぐ。PD の交渉と DP mode の
  突入に数秒かかる見込みなので、graphical session の列挙は change の事象で後から届いてよい（driver は待たない）。
- **外部 DP の model**（M6）: eDP 専用の `i915_edp_device` と別に、外部 DP の object（intel_dp、saved_port_bits、DPCD の cache）を TC の port ごとに
  持つ。pipe と transcoder は claim の時に空きから割り当て（TRANSCODER_EDP は使わない）、lane 数は min(sink、FIA、VBT の X4)、MST は SST に固定
  （`DP_MSTM_CTRL` = 0）。
- **branch device**（H7）: DPCD 1.3 以上の branch（USB-C→HDMI・DP の adapter）で protocol converter の最小の設定（HDMI の mode の選択だけ、DSC・
  YCbCr の変換は範囲外）と downstream の最大の TMDS clock の検査を移す。sink count = 0（adapter だけ）は disconnected として扱う。
- **lock の順**（M8）: tc の lock → power domains の mutex → DKL の spinlock。`ICL_DPCLKA_CFGCR0` は combo と共有で dpll の lock の下。aux の `put_async`
  と disconnect の順は `drv_i915_display_power_flush_work` で揃える。WS050 への `typec_display_report` は tc の lock の外、thread の文脈で、block しない。
- **向き**（M9）: pin C・E（4 lane）では FIA の lane mask は向きによらず 0xF、DP-alt でない接続（USB だけ、充電）では i915 に何も出ない。5330 の ACPI
  に PMC mux・IOM の device も見当たらない。**i915 から向きが取れるのは、取れたとしても pin D の DP-alt の時だけ**の見込み（推測）。WS050 の決定 4 の
  受け入れの見直しを Q1 経由でユーザーに仰ぐ。p002b の試験には pin D の adapter（USB-A 付きの multiport）と pin C の adapter の両方が要る。
- **S0ix**（M10）: suspend で pipe を止め PHY を disconnect（`intel_tc_port_suspend` 相当）、ACPI の TCSS の D3cold（ssdt7）より前に PHY を手放す。
  resume で init_mode と detect をやり直す（WS052 の device の表の i915 の行に載せる）。
- **TCSS_DDI_STATUS の pin・HPD_LIVE**（L2）: Linux 6.8 が pin の field を使うのは display 20（LNL）だけで、HPD_LIVE の bit は未使用。ADL-P では
  診断の値として読み、判断には FIA と DE の HPD を使う。
- **試験の環境**（H6）: zedBSD の i915 の「実機」の試験は 5330 の上の QEMU の VFIO の passthrough（`plan/ws075/tests/test-hw.sh`、GOP は走らない）。
  VFIO では FIA・TCSS・DDI は guest から見えるが、DP mode の突入と TCSS の電源は host の Debian（ucsi_acpi、thunderbolt、xHCI の runtime PM）が握り、
  UAT の (1)・TC cold の結果を汚す。native（zedBSD を 5330 で直接起動）か、VFIO で host の typec・thunderbolt と TCSS の PM を止めるか、正解値を
  bare metal の Linux で採るか（共有の試験 host の vfio-pci を外す、他の担当の `/tmp/i915-hw.lock` を止める）を §13 で決める。正解値は debugfs の
  `i915_shared_dplls_info` と `drm.debug=0x1e` の log を正にする（L3。`intel_reg` での DKL の index の読みは参考）。

## 13. 人間の判断が要る点（第 3 版）

1. **試験・正解値の環境**（H6）: (a) UAT を native（zedBSD を 5330 で直接起動）で行うか VFIO で行うか、(b) VFIO なら host の typec・thunderbolt の
   driver と TCSS の PM を止めてよいか、(c) bare metal の Linux（5330）で register の正解値を採る手順（kernel の版、vfio-pci の解除、試験の lock、
   他の担当の停止）の承認。
2. **向き**（M9）: WS050 の決定 4（1.x でも向きを i915 から取る）は、取れても pin D の DP-alt の時だけの見込み。受け入れを「i915 から取れる時だけ
   （pin D）、それ以外は不明」にしてよいか。

## ユーザーの決定（2026-10-04、§10）

1. TBT-alt（Thunderbolt の dock の先の DP）は**範囲外**（クリックの回答「範囲外」）。
2. 画面の model（ユーザーの回答、原文）:「ドライバは出力先を変更しない。ドライバの出力先については、GOPから引き継ぐときに、GOPの出力先を、ドライバの出力先とする。現状はこれができていないのでドライバの修正が必要。起動時にUSB-Cがあっても、GOPの出力先をドライバの出力先にする。グラフィカルセッションが起動したとき、Keilandが独自の判断で、外部ディスプレイの優先などを決定する。GOPがUSB-Cに出力されていない限り、ドライバはUSB-Cに出力しない。」
   → (a) i915 は GOP から引き継ぐ時、GOP が出していた出力先（pipe・port）をそのまま driver の出力先にする。driver は自分の判断で出力先を変えない（外部の display を優先して panel を消す今の挙動はやめる）。今の i915 はこれができていないので driver の修正が要る（WS051 の Phase として計画、WS075 の takeover.c に及ぶ）。(b) 起動時に USB-C の display が繋がっていても、GOP の出力先を使う。(c) graphical session の後は Keiland が外部の display の優先などを決め、driver は Keiland の指示（display の UAPI・libvulkan の経路）で出力先を変える。(d) GOP が USB-C に出していない限り、driver は自分から USB-C に出力しない。
3. hotplug: 2 の決定により、driver は出力先を自分で切り替えず、抜き差しを事象として Keiland（WS132 の /dev/system・display の事象）に知らせ、切り替えは Keiland の判断（Q1 の読み、2 の (c)(d) から）。
4. 補足（2026-10-04 ユーザー、原文）:「USB-C DPがアタッチされたとき、KeilandがVulkan Display extensionで通知を受けます。この通知を受けたKeilandが、設定ファイルの記録などから総合的に、ミラーや拡張などの判断を行います。出力オフもありえます。」→ hotplug の通知の経路は Vulkan の Display の拡張（libvulkan、WS113 の i915 の接続通知と同じ）。Keiland が設定の記録などから mirror・拡張・出力 off を決める。driver は通知を出し、Keiland の指示で出力するだけ（3 の Q1 の読みのうち、通知の経路は /dev/system でなく Vulkan の Display の拡張に訂正）。

補足（2026-10-04 夜 Q1）: Guardrail の scanout の規則の i915 での実装（GOP の出力先の引き継ぎ、外部優先の廃止）は WS113 p002（HDMI・eDP・DP）と WS051 p002（USB-C の DP-alt）で同じ `takeover.c`・`output.c` に触れる。先に入れた方の実装を他方が使い、重ねて書かない（2026-10-04 ユーザーの Settings の Display の頁の要望、[WS113](../ws113/ws.md)）。
