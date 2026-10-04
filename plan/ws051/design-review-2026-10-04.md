# WS051 design.md 第 1 版の敵対的レビュー（2026-10-04、P1 generation16 が起動した design-reviewer の報告の要約の記録）

読みのみ（build・試験・実機は未実施）。根拠の行番号は p1 の worktree の 2026-10-04 の時点。全項目の扱いは [design.md](design.md) §11・§12。

結論: 方針（ADL-P の adlp_tc_phy_ops に従い UCSI に依存しない）は正しい。高 8 件は p002 の前に design の改訂が要る。

## 高

- H1: TC の AUX の power domain の計算（`dp-internal.h:1204-1212`、`modeset-internal.h:548`、`pipe.c:267-285` の `AUX_A + (aux_ch - AUX_CH_A)`）が
  `AUX_CH_USBC1 = AUX_CH_D` のため TC1 で AUX_D を点け、AUX_USBC1 を点けない。Linux の d13 の port の domain の表（`intel_display_power.c:2406-2430`）に。
- H2: AUX_USBC の well の enable に TC の分岐（`icl_tc_phy_aux_power_well_enable`: `DP_AUX_CH_CTL_TBT_IO` の消去、`DKL_CMN_UC_DW_27` の `UC_HEALTH` の待ち）
  が無い（`power.c:466-555`）。DKL の index の読みが p002 で要るので Phase の依存が逆。
- H3: ADL-P の DKL の buffer translation は `adlp_get_dkl_buf_trans`・`adlp_dkl_phy_trans_dp_hbr`・`_hbr2_hbr3`（tgl ではない）。
- H4: 移植の範囲に必須の関数（`icl_program_mg_dp_mode`、`intel_tc_port_set_fia_lane_count`、`adlp_tbt_to_dp_alt_switch_wa`、`get_pin_assignment_mask`、
  `icl_update_active_dpll`、`gen11_hpd_irq_handler`、`intel_ddi_hotplug`、`tc_port_power_domain`、`init_mode`・`sanitize_mode`、link reset）と、置き換える
  stub（`modeset-internal.h`・`dp-internal.h`・`takeover-internal.h`・`hotplug-internal.h`）が無い。
- H5: DE の HPD の割り込みは `interrupts.c:1166-1174` で ack されるだけで hotplug に渡らない。
- H6: zedBSD の i915 の実機試験は 5330 の上の VFIO の passthrough。host の typec・thunderbolt・TCSS の PM が DP mode の突入と TC cold を汚す。
  native か VFIO か、bare metal の正解値の採取の手順を決める要。
- H7: USB-C→HDMI・DP の adapter（DP の branch）は `intel_dp_configure_protocol_converter` などの GUARD で error。downstream の上限と sink count=0 の扱いも無い。
- H8: `takeover.c:1271-1274` の TC PLL の enable が `0x46030 + 4n`（MG_PLL_ENABLE）。ADL-P は `ADLP_PORTTC_PLL_ENABLE`（TC1 0x46038、間隔 8）。

## 中

- M1: Linux の状態機械の TBT_ALT は「未所有」と既定の mode を兼ねる。ADL-P に TC cold off の well は無い（PCODE の主張は誤り）。
- M2: 選ばなかった port の PHY の ownership を同期で手放す手順が無い（firmware は他の port の HPD を止める）。
- M3: link training の fallback が無い。M4: 抜き差しの 2 秒の猶予・5 回の retry・retrain が無い。M5: 起動時の TC（init_mode・sanitize・TC PLL の readout）。
- M6: 外部 DP の object の model が未定（eDP 専用の object しか無い）。M7: Phase の分割（p004 を分ける、p003 に診断、正解値は外部の前提）。
- M8: lock の順。M9: 向きを i915 から取るのは、取れても pin D の DP-alt の時だけの見込み。M10: S0ix の口。

## 低

- L1: VBT の DVO port の code の誤りの主張は過大（code 15 は zedBSD でも TC2 に写り、DP か HDMI かは device_type で決まる。誤るのは code 17〜21 の
  TC3・TC4 だけ）。L2: TCSS_DDI_STATUS の pin・HPD_LIVE は ADL-P では Linux が使わない。L3: 正解値は debugfs と drm.debug を正に。L4: weak の口は理由の comment。

## 問題の無かった点

ADL-P の分担（ready・ownership・live status・TC cold の domain・modular FIA）、connect の順と巻き戻し、PLL の構成、DDI の TC の clock、xelpd の
power map の AUX_USBC・DDI_IO_TC、aux.c の USBC の register の選択、`intel_ddi_init_dp_buf_reg` の TC の分岐、向きの bit が register の定義に無いこと。
