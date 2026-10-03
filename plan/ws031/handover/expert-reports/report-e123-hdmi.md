# WS031 増分 E-123 報告: OpRegion の実書込み、HDMI の接続検出、外部ディスプレイの点灯

2026-09-20 / zedBSD Linux-parity (i915, Linux v6.8.12 正本) / 対象機 Dell Latitude 5330 (ADL-P, PCH ADP)

利用者の指示は「OpRegion 書込みの実装・テスト、HDMI 接続・切断の通知受信の実装・テスト、外部ディスプレイの有効化・無効化の制御の実装・テスト、について実機テストまでやってから、その先の作業に進みましょう」。3 項目とも実機で確認できた。

## 1. 結果

| 項目 | 実機での結果 |
|---|---|
| OpRegion の実書込み | **native PASS (5/5)**。setup で CHPD 1 / ARDY 0、register で DRDY 1 / ARDY 1、合成 ACPI 通知と ASLE 要求に応答、unregister で DRDY 0 / ARDY 0、cleanup rc 0。続く N0 は従来どおり STOP、hang なし |
| HDMI 接続・切断の受信 | **実機 PASS**。抜き差しに対し disconnected → connected → disconnected → connected（CHANGED、live 1/0/1、epoch 4）、storm 0、WARN 0、取りこぼし 0 |
| 外部ディスプレイの有効化・無効化 | **実機 PASS**。DDI B / pipe B / transcoder B / DPLL0、1280×720@60 で pattern 110 を表示（利用者が目視確認）、参照の停止経路で消灯、停止後 TRANSCONF=0・frame counter 停止・資源全返却 |

GPU なしの model 試験は 612 checks / 0 failures、host 試験は 56/0 と 123/0、生成物の再現性チェックも通っている。

## 2. 移植したもの（すべて正本のテキストを生成）

### 2.1 HDMI hotplug の受信経路
`icp_irq_handler` → `intel_get_hpd_pins`（+ `icp_ddi_port_hotplug_long_detect`）→ `intel_hpd_irq_handler`（storm 検出）→ `i915_hotplug_work_func` → `intel_ddi_hotplug`（RETRY）→ `drm_helper_probe_detect`（epoch）→ `intel_hdmi_detect` → `intel_digital_port_connected`（`lpt_digital_port_connected`: SDEISR & pch_hpd）。

生成 unit: `intel_hotplug_port.c`、`intel_hotplug_irq_port.c`、`intel_ddi_hotplug_port.c`、`intel_dp_connected_port.c`、`intel_hdmi_detect_port.c`、`drm_probe_detect_port.c`、`drm_connector_status_port.c`。compat は `lcd/hpd_compat.h`（kernel の spinlock / mutex、共有 kworkqueue と ktimerq、`sched_ticks()` を jiffies として使用）。この unit の global はすべて `parity_hpd_` 接頭辞でリンクする。

正本に無い DRM core の 2 ファイル（`drm_probe_helper.c`、`drm_connector.c`）は kernel.org の v6.8.12 から取得し、README と SHA256SUMS に記録した。

### 2.2 EDID（GMBUS）
`intel_gmbus_port.c`（`struct intel_gmbus`、`gmbus_wait` / `_idle`、read/write chunk、index 転送、`do_gmbus_xfer` の NAK 再試行と bit-banging への切替、`gmbus_xfer`、`force_bit`、`intel_gmbus_irq_handler`）。`intel_hdmi_set_edid` も正本テキストに置換。EDID の読み出しは既存の `parity_drm_edid_read`（正本 `drm_do_probe_ddc_edid`）を GMBUS の i2c adapter に対して使う。

### 2.3 HDMI の modeset
WRPLL の計算（`icl_wrpll_ref_clock` / `icl_wrpll_get_multipliers` / `icl_wrpll_params_populate` / `icl_calc_wrpll`）、DDI の HDMI 分岐（`intel_ddi_hdmi_level`、`intel_ddi_pre_enable_hdmi`、`intel_enable_ddi_hdmi`、`intel_disable_ddi_hdmi`、`intel_ddi_post_disable_hdmi`。従来の placeholder は撤去）、`intel_hdmi_mode_port.c`（`assert_hdmi_transcoder_func_disabled`、`hsw_set_infoframes`、`intel_dp_dual_mode_set_tmds_output`、`intel_hdmi_handle_sink_scrambling`）。

modeset object は出力種別（`cfg->output_hdmi`）を持ち、HDMI では DP 固有の経路（link M/N、enhanced framing、backlight、リンク訓練の確認）を通らない。

## 3. 適応（記録し、隠していない）

1. **EDID が読めない**。GMBUS が 0x50 で NAK を返し、参照どおり 1 回再試行し、bit-banging に切り替えても読めない。**同じ VM 構成の Linux 6.8 が同一の列を出す**（`NAK for addr: 0050 w(1)` → retry → `skipping non-existent adapter` → bit-banging → disconnected）。モニターを替えても同じ。移植ではなく環境要因。
   - このため点灯時のモードは EDID からではなく CEA-861 format 4（1280×720p60）を与えている。`has_hdmi_sink=0`（DVI mode、infoframe なし）。
2. connector は hotplug 経路の開始時に作る（正本は `intel_ddi_init` 内）。
3. `intel_dp_hpd_pulse` / `intel_dp_detect` / `intel_tc_port_connected` は step として記録（`hpd_pulse` は正本が eDP の long pulse に返すのと同じ IRQ_HANDLED）。
4. polling（`drm_kms_helper_poll_*`）と uevent は計数のみ。GMBUS の wait queue は起床の計数のみ。
5. VBT の HDMI level shift は未読。正本が VBT に値が無いときに使う buf trans の既定 entry を使う。
6. SCDC（scrambling）と DP dual-mode adaptor の DDC 操作は step。どちらも今回の条件では正本も実行しない経路。

## 4. 途中で見つけて直した不具合

1. **HPD の model 試験が実機割込みとデッドロック**。model 試験がスレッド文脈で IRQ 入口を呼び、同一 CPU に実機の SDE 割込みが入って `irq_lock` を取り合った（回帰 sweep の eu で顕在化）。実機入口は model 稼働中は何もせず、model には割込み禁止の専用入口を用意した。
2. **errno の不一致**。GMBUS 側と EDID リーダー側で ENXIO の番号が違い、NAK の判定が効かなかった。compat で Linux 番号に統一。
3. 判定側の誤り 3 件（HDMI に DP のリンク確認を適用、修正イメージの転送漏れ、report の memset で判定フラグを消去）。点灯自体は初回から成功していた。
4. `lcd_compat.h` を誤って git から戻し、E-122 の変更（`drm_connector_state.crtc`）を一時的に失った。復旧済み、host 試験と生成チェックで確認。

## 5. 未解決・次の作業

- **EDID の取得**。ポートを有効にしたあとに再読み出しを試す価値がある（リタイマやレベルシフタが TMDS 出力で動く設計の可能性）。
- **2 本目の pipe との同時運用**。今回は pipe A を止めた状態で pipe B のみ。両方を同時に点けるには DBUF/MBUS の再配分（pipe A の DDB と watermark の再計算）が要る。
- **infoframe と音声、HDCP、scrambling**（340 MHz 超）。今回の条件では通らない経路。
- **N1（firmware display の引継ぎ）**。native で pipe A が active のまま止まっている状態の readout と停止。
- 回帰 sweep（14 モード）は E-123 の全変更を含めて再実行中。

## 6. 記録

台帳 `plan/ws031/results-ws031.md`（増分 E-123 とその追記）、ログ `handover/increment-results/e123-run-parity-hw-hpd.log`、`e123-run-parity-hw-hdmi-edid.log`、`e123-run-parity-hw-hdmib.log`、写真 `e123-native-opregion-fw.webp`、`e123-native-n0.jpg`。作業 script は `handover/tools/lcd-e123/`（round 73〜84）。
